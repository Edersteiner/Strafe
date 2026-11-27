#include "console/builtin_commands.h"

#include "strafe/console.h"
#include "strafe/core/type_registry.h"
#include "strafe/core/utils.h"
#include "strafe/ecs/components.h"
#include "strafe/ecs/registry.h"
#include "strafe/ecs/system.h"
#include "strafe/engine.h"
#include "strafe/input.h"
#include "strafe/world.h"

#include <algorithm>
#include <format>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace strafe::console {

namespace {

std::string describe(const input::InputBinding &binding) {
  return std::visit(
      [&](auto source) {
        using Source = decltype(source);
        const std::string_view name = reflect::enumName(source).value_or("?");
        if constexpr (std::is_same_v<Source, input::GamepadAxis>) {
          const bool negative = binding.axis_direction == input::AxisDirection::Negative;
          return std::format("{}{} axis", negative ? "-" : "+", name);
        } else if constexpr (std::is_same_v<Source, input::MouseButton>) {
          return std::format("{} mouse button", name);
        } else if constexpr (std::is_same_v<Source, input::GamepadButton>) {
          return std::format("{} button", name);
        } else {
          return std::string(name);
        }
      },
      binding.source);
}

std::string join(std::span<const std::string_view> names) {
  std::string text;
  for (const std::string_view name : names) {
    text += std::format("{}{}", text.empty() ? "" : ", ", name);
  }
  return text;
}

std::string_view describe(reflect::TypeKind kind) {
  switch (kind) {
  case reflect::TypeKind::Builtin:
    return "builtin";
  case reflect::TypeKind::Record:
    return "record";
  case reflect::TypeKind::Enum:
    return "enum";
  }
  return "?";
}

void printHelp(const Command &command) {
  if (command.help.empty()) {
    Console::print("{}", command.usage());
  } else {
    Console::print("{}: {}", command.usage(), command.help);
  }
}

} // namespace

void help(const std::optional<CommandName> &command) {
  const CommandRegistry &commands = Console::commands();
  if (command) {
    if (const Command *found = commands.find(command->name)) {
      printHelp(*found);
    } else {
      log::error("There is no command '{}'", command->name);
    }
    return;
  }
  for (const std::string &name : commands.names()) {
    printHelp(*commands.find(name));
  }
}

void clear() { Console::clear(); }

void echo(const RestOfLine &text) { Console::print("{}", text.text); }

void quit() { Engine::quit(); }

void logLevel(std::optional<log::Level> level) {
  if (level) {
    log::setLevel(*level);
  }
  Console::print("The log level is {}", reflect::enumName(log::getLevel()).value_or("?"));
}

void debugView(std::optional<renderer::DebugView> view) {
  if (view) {
    Renderer::setDebugView(*view);
  }
  Console::print("The debug view is {}", reflect::enumName(Renderer::debugView()).value_or("?"));
}

void actions() {
  for (const std::string &name : Input::actionNames()) {
    std::string bindings;
    for (const input::InputBinding &binding : Input::bindings(name)) {
      bindings += std::format("{}{}", bindings.empty() ? "" : ", ", describe(binding));
    }
    Console::print("{}: {}", name, bindings.empty() ? "not bound" : bindings);
  }
}

void types() {
  for (const reflect::TypeDesc &type : Engine::types().types()) {
    Console::print("{} ({}, {} bytes)", type.name, describe(type.kind), type.size);
  }
}

void entities() {
  ecs::Registry &registry = World::entities();
  for (const ecs::Entity entity : registry.entities()) {
    std::string components;
    for (const std::unique_ptr<ecs::PoolBase> &pool : registry.pools()) {
      if (pool->contains(entity)) {
        components += std::format("{}{}", components.empty() ? "" : ", ", pool->name());
      }
    }
    const Name *name = registry.tryGet<Name>(entity);
    Console::print(
        "{}{}: {}", entity, name ? std::format(" '{}'", name->value) : "",
        components.empty() ? "no components" : components);
  }
  Console::print("{} {}", registry.size(), registry.size() == 1 ? "entity" : "entities");
}

void destroy(ecs::Entity entity) { World::entities().destroy(entity); }

bool Argument<SystemName>::read(ArgumentReader &in, SystemName &value) {
  std::string_view word;
  if (!in.readWord(word)) {
    return false;
  }
  if (!Engine::systems().find(word)) {
    return in.fail(std::format("there is no system '{}'", word));
  }
  value.name = word;
  return true;
}

std::vector<std::string> Argument<SystemName>::choices() {
  std::vector<std::string> names;
  for (const auto &enumerator : reflect::TypeInfo<ecs::Stage>::enumerators) {
    for (const ecs::SystemDesc *system : Engine::systems().stage(enumerator.value)) {
      names.emplace_back(system->name);
    }
  }
  std::ranges::sort(names);
  return names;
}

void systems() {
  for (const auto &enumerator : reflect::TypeInfo<ecs::Stage>::enumerators) {
    Console::print("{}:", utils::toLower(enumerator.name));
    for (const ecs::SystemDesc *system : Engine::systems().stage(enumerator.value)) {
      std::string access;
      if (!system->writes.empty()) {
        access += std::format(", writes {}", join(system->writes));
      }
      if (!system->reads.empty()) {
        access += std::format(", reads {}", join(system->reads));
      }
      Console::print("  {}{}{}", system->name, system->enabled ? "" : " (off)", access);
    }
  }
}

void systemEnabled(const SystemName &name, std::optional<bool> enabled) {
  ecs::SystemDesc &found = *Engine::systems().find(name.name);
  if (enabled) {
    found.enabled = *enabled;
  }
  Console::print("{} is {}", found.name, found.enabled ? "on" : "off");
}

} // namespace strafe::console
