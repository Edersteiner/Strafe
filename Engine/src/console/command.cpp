#include "strafe/console/command.h"

#include "strafe/console.h"
#include "strafe/core/log.h"
#include "strafe/core/utils.h"
#include "strafe/ecs/components.h"
#include "strafe/ecs/registry.h"
#include "strafe/world.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>
#include <string>
#include <system_error>
#include <vector>

namespace strafe::console {

namespace {

bool isSpace(char c) { return c == ' ' || c == '\t'; }

// Splits a line at spaces. Quotes keep spaces in a word, and a backslash inside
// them keeps the next character. Nothing when a quote is never closed.
std::optional<std::vector<std::string>> splitWords(std::string_view line) {
  std::vector<std::string> words;
  size_t i = 0;
  while (true) {
    while (i < line.size() && isSpace(line[i])) {
      ++i;
    }
    if (i == line.size()) {
      return words;
    }
    std::string word;
    bool quoted = false;
    for (; i < line.size() && (quoted || !isSpace(line[i])); ++i) {
      if (line[i] == '"') {
        quoted = !quoted;
      } else if (quoted && line[i] == '\\' && i + 1 < line.size()) {
        word += line[++i];
      } else {
        word += line[i];
      }
    }
    if (quoted) {
      return std::nullopt;
    }
    words.push_back(std::move(word));
  }
}

bool startsWithIgnoringCase(std::string_view text, std::string_view prefix) {
  return utils::toLower(text).starts_with(utils::toLower(prefix));
}

std::string describe(const Parameter &parameter) {
  return parameter.name.empty() ? parameter.type : std::format("{}: {}", parameter.name, parameter.type);
}

template <typename T> bool parseNumber(std::string_view word, T &value) {
  const auto [end, error] = std::from_chars(word.data(), word.data() + word.size(), value);
  return error == std::errc() && end == word.data() + word.size();
}

} // namespace

bool ArgumentReader::fail(std::string_view message) const {
  if (parameter_ < parameters_.size() && !parameters_[parameter_].name.empty()) {
    log::error("{}: {}: {}", command_, parameters_[parameter_].name, message);
  } else {
    log::error("{}: {}", command_, message);
  }
  return false;
}

bool ArgumentReader::readWord(std::string_view &value) {
  if (empty()) {
    const std::string what =
        parameter_ < parameters_.size() ? describe(parameters_[parameter_]) : std::string("an argument");
    log::error("{}: missing {}", command_, what);
    return false;
  }
  value = words_[next_++];
  return true;
}

bool ArgumentReader::readRest(std::string &value) {
  value.clear();
  while (!empty()) {
    if (!value.empty()) {
      value += ' ';
    }
    value += words_[next_++];
  }
  return true;
}

bool ArgumentReader::readBool(bool &value) {
  std::string_view word;
  if (!readWord(word)) {
    return false;
  }
  const std::string lower = utils::toLower(word);
  if (lower == "true" || lower == "on" || lower == "yes" || lower == "1") {
    value = true;
    return true;
  }
  if (lower == "false" || lower == "off" || lower == "no" || lower == "0") {
    value = false;
    return true;
  }
  return fail(std::format("expected true or false, found '{}'", word));
}

bool ArgumentReader::readInt(int64_t &value, int64_t min, int64_t max) {
  std::string_view word;
  if (!readWord(word)) {
    return false;
  }
  int64_t parsed = 0;
  if (!parseNumber(word, parsed)) {
    return fail(std::format("expected an integer, found '{}'", word));
  }
  if (parsed < min || parsed > max) {
    return fail(std::format("expected an integer from {} to {}, found '{}'", min, max, word));
  }
  value = parsed;
  return true;
}

bool ArgumentReader::readUint(uint64_t &value, uint64_t max) {
  std::string_view word;
  if (!readWord(word)) {
    return false;
  }
  uint64_t parsed = 0;
  if (!parseNumber(word, parsed)) {
    return fail(std::format("expected an integer of 0 or more, found '{}'", word));
  }
  if (parsed > max) {
    return fail(std::format("expected an integer from 0 to {}, found '{}'", max, word));
  }
  value = parsed;
  return true;
}

bool ArgumentReader::readNumber(double &value) {
  std::string_view word;
  if (!readWord(word)) {
    return false;
  }
  double parsed = 0.0;
  if (!parseNumber(word, parsed) || !std::isfinite(parsed)) {
    return fail(std::format("expected a number, found '{}'", word));
  }
  value = parsed;
  return true;
}

bool ArgumentReader::readFloats(std::initializer_list<float *> values) {
  for (float *value : values) {
    double number = 0.0;
    if (!readNumber(number)) {
      return false;
    }
    *value = static_cast<float>(number);
  }
  return true;
}

bool ArgumentReader::readName(size_t &index, std::span<const std::string_view> candidates) {
  std::string_view word;
  if (!readWord(word)) {
    return false;
  }
  const std::string lower = utils::toLower(word);
  const auto same = [&](std::string_view candidate) { return utils::toLower(candidate) == lower; };
  auto it = std::ranges::find(candidates, word);
  if (it == candidates.end()) {
    it = std::ranges::find_if(candidates, same);
  }
  if (it != candidates.end()) {
    index = static_cast<size_t>(it - candidates.begin());
    return true;
  }

  std::string message = std::format("unknown value '{}'", word);
  if (const std::optional<std::string_view> suggestion = utils::closestMatch(word, candidates)) {
    message += std::format("; did you mean '{}'?", *suggestion);
  } else {
    message += "; expected one of ";
    for (size_t i = 0; i < candidates.size(); ++i) {
      message += std::format("{}{}", i > 0 ? ", " : "", candidates[i]);
    }
  }
  return fail(message);
}

bool ArgumentReader::finish() {
  if (empty()) {
    return true;
  }
  log::error("{}: unexpected '{}'", command_, words_[next_]);
  return false;
}

bool Argument<CommandName>::read(ArgumentReader &in, CommandName &value) {
  std::string_view word;
  if (!in.readWord(word)) {
    return false;
  }
  value.name = word;
  return true;
}

std::vector<std::string> Argument<CommandName>::choices() { return Console::commands().names(); }

bool Argument<ecs::Entity>::read(ArgumentReader &in, ecs::Entity &value) {
  std::string_view word;
  if (!in.readWord(word)) {
    return false;
  }
  ecs::Registry &registry = World::entities();
  const size_t colon = word.find(':');
  uint32_t index = 0;
  uint32_t generation = 0;
  if (parseNumber(word.substr(0, colon), index)) {
    if (colon == std::string_view::npos) {
      value = registry.atIndex(index);
    } else if (parseNumber(word.substr(colon + 1), generation)) {
      value = {.index = index, .generation = generation};
    } else {
      return in.fail(std::format("'{}' isn't an entity; use a name, an index, or index:generation", word));
    }
  } else {
    value = registry.find(word);
  }
  if (!registry.alive(value)) {
    return in.fail(std::format("there is no entity {}", word));
  }
  return true;
}

std::vector<std::string> Argument<ecs::Entity>::choices() {
  std::vector<std::string> names;
  for (auto [entity, name] : World::entities().query<const Name>()) {
    if (!name.value.empty() && std::ranges::find(names, name.value) == names.end()) {
      names.push_back(name.value);
    }
  }
  std::ranges::sort(names);
  return names;
}

void detail::printResult(std::string_view text) { log::print("{}", text); }

std::string Parameter::usage() const {
  return optional ? std::format("[{}]", describe(*this)) : std::format("<{}>", describe(*this));
}

std::string Command::usage() const {
  std::string text = name;
  for (const Parameter &parameter : parameters) {
    text += ' ';
    text += parameter.usage();
  }
  return text;
}

void CommandRegistry::insert(Command command) {
  if (commands_.contains(command.name)) {
    log::warn("Command '{}' replaces an earlier one with the same name", command.name);
  }
  std::string name = command.name;
  commands_.insert_or_assign(std::move(name), std::make_shared<const Command>(std::move(command)));
}

void CommandRegistry::remove(std::string_view name) {
  if (const auto it = commands_.find(name); it != commands_.end()) {
    commands_.erase(it);
  }
}

const Command *CommandRegistry::find(std::string_view name) const {
  const auto it = commands_.find(name);
  return it != commands_.end() ? it->second.get() : nullptr;
}

std::vector<std::string> CommandRegistry::names() const {
  std::vector<std::string> names;
  names.reserve(commands_.size());
  for (const auto &[name, command] : commands_) {
    names.push_back(name);
  }
  return names;
}

bool CommandRegistry::run(std::string_view line) {
  const std::optional<std::vector<std::string>> words = splitWords(line);
  if (!words) {
    log::error("A quote is never closed");
    return false;
  }
  if (words->empty()) {
    return true;
  }

  const std::string &name = words->front();
  const auto it = commands_.find(name);
  if (it == commands_.end()) {
    const std::vector<std::string> known = names();
    const std::vector<std::string_view> candidates(known.begin(), known.end());
    if (const std::optional<std::string_view> suggestion = utils::closestMatch(name, candidates)) {
      log::error("Unknown command '{}'; did you mean '{}'?", name, *suggestion);
    } else {
      log::error("Unknown command '{}'. 'help' lists them all.", name);
    }
    return false;
  }

  const std::shared_ptr<const Command> command = it->second;
  ArgumentReader in(command->name, command->parameters, std::span(*words).subspan(1));
  if (!command->run(in)) {
    log::print("usage: {}", command->usage());
    return false;
  }
  return true;
}

Completion CommandRegistry::complete(std::string_view line) const {
  // The last word starts after the last space, unless a quote is open.
  if (std::ranges::count(line, '"') > 0) {
    return {.start = line.size()};
  }
  size_t start = line.size();
  while (start > 0 && !isSpace(line[start - 1])) {
    --start;
  }
  const std::string_view word = line.substr(start);
  const std::vector<std::string> before = splitWords(line.substr(0, start)).value_or(std::vector<std::string>{});

  Completion completion{.start = start};
  const auto add_matches = [&](const std::vector<std::string> &choices, bool are_commands) {
    for (const std::string &choice : choices) {
      if (startsWithIgnoringCase(choice, word)) {
        const Command *command = are_commands ? find(choice) : nullptr;
        completion.candidates.push_back({.text = choice, .detail = command ? command->help : std::string()});
      }
    }
  };

  if (before.empty()) {
    add_matches(names(), true);
    return completion;
  }
  completion.command = find(before.front());
  if (!completion.command) {
    return completion;
  }
  // Which parameter the word belongs to, counting the words of those before.
  const std::vector<Parameter> &parameters = completion.command->parameters;
  size_t position = before.size() - 1;
  while (completion.parameter < parameters.size()) {
    const Parameter &parameter = parameters[completion.parameter];
    if (parameter.words == 0 || position < parameter.words) {
      if (parameter.choices) {
        add_matches(parameter.choices(), parameter.choices == &Argument<CommandName>::choices);
      }
      break;
    }
    position -= parameter.words;
    ++completion.parameter;
  }
  return completion;
}

} // namespace strafe::console
