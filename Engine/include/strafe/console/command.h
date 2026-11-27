#pragma once

#include "strafe/app.h"
#include "strafe/core/math.h"
#include "strafe/core/reflect.h"

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace strafe::console {

struct Parameter {
  // Empty when the command didn't name it.
  std::string name;
  std::string type;
  bool optional = false;
  // How many words it takes. Zero takes the rest of the line.
  size_t words = 1;
  // The values it accepts, for completion. Null when there is no list.
  std::vector<std::string> (*choices)() = nullptr;

  // "<position: Vec3>", or in square brackets when it can be left out.
  std::string usage() const;
};

// The words after a command's name, taken in order by its parameters. Every
// read logs an error naming the command and parameter when the words don't fit.
class ArgumentReader {
public:
  ArgumentReader(std::string_view command, std::span<const Parameter> parameters, std::span<const std::string> words)
      : command_(command), parameters_(parameters), words_(words) {}

  bool empty() const { return next_ == words_.size(); }
  // Names the parameter that the following reads are for.
  void begin(size_t parameter) { parameter_ = parameter; }

  bool readWord(std::string_view &value);
  bool readRest(std::string &value);
  bool readBool(bool &value);
  bool readInt(int64_t &value, int64_t min, int64_t max);
  bool readUint(uint64_t &value, uint64_t max);
  bool readNumber(double &value);
  bool readFloats(std::initializer_list<float *> values);
  // Sets index to the candidate the word names, ignoring case.
  bool readName(size_t &index, std::span<const std::string_view> candidates);

  // Fails when words are left over.
  bool finish();
  bool fail(std::string_view message) const;

private:
  std::string_view command_;
  std::span<const Parameter> parameters_;
  std::span<const std::string> words_;
  size_t parameter_ = 0;
  size_t next_ = 0;
};

// Specialize for a type of your own:
//
//   template <> struct console::Argument<Team> {
//     static constexpr std::string_view kType = "team";
//     static constexpr size_t kWords = 1;
//     static bool read(console::ArgumentReader &in, Team &value);
//     static std::vector<std::string> choices(); // Optional.
//   };
template <typename T> struct Argument;

template <typename T>
concept CustomArgument = requires(ArgumentReader &in, T &value) {
  { Argument<T>::read(in, value) } -> std::same_as<bool>;
  { Argument<T>::kType } -> std::convertible_to<std::string_view>;
};

// A whole line of text, spaces and all. Only makes sense as the last parameter.
struct RestOfLine {
  std::string text;
};

template <> struct Argument<RestOfLine> {
  static constexpr std::string_view kType = "text";
  static constexpr size_t kWords = 0;
  static bool read(ArgumentReader &in, RestOfLine &value) { return in.readRest(value.text); }
};

// The name of a command, completed from the ones that exist.
struct CommandName {
  std::string name;
};

template <> struct Argument<CommandName> {
  static constexpr std::string_view kType = "command";
  static constexpr size_t kWords = 1;
  static bool read(ArgumentReader &in, CommandName &value);
  static std::vector<std::string> choices();
};

struct Command {
  std::string name;
  std::string help;
  std::vector<Parameter> parameters;
  // Returns false when the arguments don't fit.
  std::function<bool(ArgumentReader &)> run;

  // "teleport <position: Vec3> [keep_velocity: bool]".
  std::string usage() const;
};

struct Candidate {
  std::string text;
  // What a command does. Empty for other values.
  std::string detail;
};

struct Completion {
  // Where the word being completed starts in the line.
  size_t start = 0;
  std::vector<Candidate> candidates;
  // The command whose argument is being typed, null while typing its name.
  // Valid until the registry changes.
  const Command *command = nullptr;
  // Into the command's parameters, or past the end after the last one.
  size_t parameter = 0;
};

class CommandRegistry {
public:
  // Adds a command that calls function, replacing one with the same name. The
  // parameter types come from its signature, and names are only for messages.
  template <typename Function>
  void
  add(std::string_view name, std::string_view help, Function &&function,
      std::initializer_list<std::string_view> parameter_names = {});

  // For generated code. Only the type of signature is used. call makes the
  // call by name, so the last Defaults arguments can be left out.
  template <size_t Defaults, typename Result, typename... Params, typename Call>
  void addFunction(
      std::string_view name, std::string_view help, std::initializer_list<std::string_view> parameter_names,
      Result (*signature)(Params...), Call call);

  // The same for a member function of the game's App. Skipped when another App
  // is the one that runs.
  template <size_t Defaults, typename Class, typename Result, typename... Params, typename Call>
  void addMember(
      std::string_view name, std::string_view help, std::initializer_list<std::string_view> parameter_names,
      Result (Class::*signature)(Params...), Call call);
  template <size_t Defaults, typename Class, typename Result, typename... Params, typename Call>
  void addMember(
      std::string_view name, std::string_view help, std::initializer_list<std::string_view> parameter_names,
      Result (Class::*signature)(Params...) const, Call call);

  // What member commands run on. Set before they are added.
  void setApp(App &app) { app_ = &app; }

  void remove(std::string_view name);
  const Command *find(std::string_view name) const;
  // Sorted.
  std::vector<std::string> names() const;

  // Runs one line. Logs what is wrong when it can't.
  bool run(std::string_view line);
  // What could stand at the end of line.
  Completion complete(std::string_view line) const;

private:
  template <size_t Defaults, typename Class, typename Signature, typename Call>
  void addOnApp(
      std::string_view name, std::string_view help, std::initializer_list<std::string_view> parameter_names,
      const Signature *signature, Call call);
  void insert(Command command);

  App *app_ = nullptr;
  // Shared, so a command that removes itself survives until it returns.
  std::map<std::string, std::shared_ptr<const Command>, std::less<>> commands_;
};

namespace detail {

template <typename T> inline constexpr bool kIsOptional = false;
template <typename T> inline constexpr bool kIsOptional<std::optional<T>> = true;
template <typename T> inline constexpr bool kUnsupported = false;

// The part of a qualified name after the last scope.
constexpr std::string_view unqualified(std::string_view name) {
  const size_t scope = name.rfind("::");
  return scope == std::string_view::npos ? name : name.substr(scope + 2);
}

template <typename T> constexpr std::string_view typeLabel() {
  if constexpr (CustomArgument<T>) {
    return Argument<T>::kType;
  } else if constexpr (kIsOptional<T>) {
    return typeLabel<typename T::value_type>();
  } else if constexpr (std::same_as<T, bool>) {
    return "bool";
  } else if constexpr (std::integral<T>) {
    return std::is_signed_v<T> ? "int" : "uint";
  } else if constexpr (std::floating_point<T>) {
    return "number";
  } else if constexpr (std::same_as<T, std::string> || std::same_as<T, std::string_view>) {
    return "string";
  } else if constexpr (std::same_as<T, Vec2>) {
    return "Vec2";
  } else if constexpr (std::same_as<T, Vec3>) {
    return "Vec3";
  } else if constexpr (std::same_as<T, Vec4>) {
    return "Vec4";
  } else if constexpr (reflect::ReflectedEnum<T>) {
    return unqualified(reflect::typeName<T>());
  } else {
    static_assert(
        kUnsupported<T>,
        "This type can't be a command parameter. Use a bool, number, string, vector, [[reflect]] enum "
        "or std::optional of one, or specialize console::Argument for it.");
  }
}

template <typename T> constexpr size_t wordCount() {
  if constexpr (CustomArgument<T>) {
    return Argument<T>::kWords;
  } else if constexpr (kIsOptional<T>) {
    return wordCount<typename T::value_type>();
  } else if constexpr (std::same_as<T, Vec2>) {
    return 2;
  } else if constexpr (std::same_as<T, Vec3>) {
    return 3;
  } else if constexpr (std::same_as<T, Vec4>) {
    return 4;
  } else {
    return 1;
  }
}

template <typename T> constexpr auto choicesOf() -> std::vector<std::string> (*)() {
  if constexpr (kIsOptional<T>) {
    return choicesOf<typename T::value_type>();
  } else if constexpr (requires { Argument<T>::choices(); }) {
    return &Argument<T>::choices;
  } else if constexpr (std::same_as<T, bool>) {
    return [] { return std::vector<std::string>{"true", "false"}; };
  } else if constexpr (reflect::ReflectedEnum<T>) {
    return [] {
      constexpr auto kNames = reflect::enumNames<T>();
      return std::vector<std::string>(kNames.begin(), kNames.end());
    };
  } else {
    return nullptr;
  }
}

template <typename T> bool readArgument(ArgumentReader &in, T &value) {
  if constexpr (CustomArgument<T>) {
    return Argument<T>::read(in, value);
  } else if constexpr (kIsOptional<T>) {
    return readArgument(in, value.emplace());
  } else if constexpr (std::same_as<T, bool>) {
    return in.readBool(value);
  } else if constexpr (std::integral<T> && std::is_signed_v<T>) {
    int64_t number = 0;
    const bool ok = in.readInt(number, std::numeric_limits<T>::min(), std::numeric_limits<T>::max());
    value = static_cast<T>(number);
    return ok;
  } else if constexpr (std::integral<T>) {
    uint64_t number = 0;
    const bool ok = in.readUint(number, std::numeric_limits<T>::max());
    value = static_cast<T>(number);
    return ok;
  } else if constexpr (std::floating_point<T>) {
    double number = 0.0;
    const bool ok = in.readNumber(number);
    value = static_cast<T>(number);
    return ok;
  } else if constexpr (std::same_as<T, std::string_view>) {
    return in.readWord(value);
  } else if constexpr (std::same_as<T, std::string>) {
    std::string_view word;
    const bool ok = in.readWord(word);
    value = word;
    return ok;
  } else if constexpr (std::same_as<T, Vec2>) {
    return in.readFloats({&value.x, &value.y});
  } else if constexpr (std::same_as<T, Vec3>) {
    return in.readFloats({&value.x, &value.y, &value.z});
  } else if constexpr (std::same_as<T, Vec4>) {
    return in.readFloats({&value.x, &value.y, &value.z, &value.w});
  } else if constexpr (reflect::ReflectedEnum<T>) {
    static constexpr auto kNames = reflect::enumNames<T>();
    size_t index = 0;
    if (!in.readName(index, kNames)) {
      return false;
    }
    value = reflect::TypeInfo<T>::enumerators[index].value;
    return true;
  } else {
    // typeLabel reports the unsupported type.
    return false;
  }
}

// How many leading parameters must be given. The rest have a default argument
// or are optional.
template <size_t Defaults, typename... Params> consteval size_t requiredCount() {
  constexpr std::array<bool, sizeof...(Params)> kOptional{kIsOptional<std::remove_cvref_t<Params>>...};
  size_t required = sizeof...(Params) - Defaults;
  while (required > 0 && kOptional[required - 1]) {
    --required;
  }
  return required;
}

void printResult(std::string_view text);

// Shows what a command returned, for the types that read as text.
template <typename Call> void callAndPrint(Call &&call) {
  using Result = std::remove_cvref_t<decltype(call())>;
  if constexpr (std::is_void_v<Result>) {
    call();
  } else if constexpr (std::same_as<Result, bool>) {
    printResult(call() ? "true" : "false");
  } else if constexpr (std::is_arithmetic_v<Result>) {
    printResult(std::to_string(call()));
  } else if constexpr (std::convertible_to<Result, std::string_view>) {
    printResult(call());
  } else {
    static_cast<void>(call());
  }
}

// Calls with the first Arity values, each passed the way its parameter takes it.
template <size_t Arity, typename... Params, typename Call, typename Values> void callWith(Call &call, Values &values) {
  using Types = std::tuple<Params...>;
  [&]<size_t... Indices>(std::index_sequence<Indices...>) {
    callAndPrint([&]() -> decltype(auto) {
      return call(static_cast<std::tuple_element_t<Indices, Types> &&>(std::get<Indices>(values))...);
    });
  }(std::make_index_sequence<Arity>());
}

template <size_t Defaults, typename... Params, typename Call> bool invoke(ArgumentReader &in, Call &call) {
  constexpr size_t kCount = sizeof...(Params);
  constexpr size_t kRequired = requiredCount<Defaults, Params...>();
  constexpr size_t kMinArity = kCount - Defaults;

  std::tuple<std::remove_cvref_t<Params>...> values;
  size_t given = 0;
  bool ok = true;
  const auto read_one = [&](auto &value, size_t index) {
    if (ok && (index < kRequired || !in.empty())) {
      in.begin(index);
      ok = readArgument(in, value);
      ++given;
    }
  };
  [&]<size_t... Indices>(std::index_sequence<Indices...>) {
    (read_one(std::get<Indices>(values), Indices), ...);
  }(std::make_index_sequence<kCount>());
  if (!ok || !in.finish()) {
    return false;
  }

  // Left out optionals are passed empty, left out defaults aren't passed.
  const size_t arity = std::max(given, kMinArity);
  [&]<size_t... Extra>(std::index_sequence<Extra...>) {
    ((arity == kMinArity + Extra ? callWith<kMinArity + Extra, Params...>(call, values) : void()), ...);
  }(std::make_index_sequence<Defaults + 1>());
  return true;
}

// signature only carries the parameter types.
template <size_t Defaults, typename Result, typename... Params, typename Call>
Command makeCommand(
    std::string_view name, std::string_view help, std::initializer_list<std::string_view> parameter_names,
    const std::function<Result(Params...)> * /* signature */, Call call) {
  constexpr size_t kRequired = requiredCount<Defaults, Params...>();
  Command command{.name = std::string(name), .help = std::string(help)};
  command.parameters = {Parameter{
      .type = std::string(typeLabel<std::remove_cvref_t<Params>>()),
      .words = wordCount<std::remove_cvref_t<Params>>(),
      .choices = choicesOf<std::remove_cvref_t<Params>>()}...};
  for (size_t i = 0; i < command.parameters.size(); ++i) {
    command.parameters[i].optional = i >= kRequired;
    if (i < parameter_names.size()) {
      command.parameters[i].name = parameter_names.begin()[i];
    }
  }
  command.run = [call = std::move(call)](ArgumentReader &in) mutable { return invoke<Defaults, Params...>(in, call); };
  return command;
}

} // namespace detail

template <typename Function>
void CommandRegistry::add(
    std::string_view name, std::string_view help, Function &&function,
    std::initializer_list<std::string_view> parameter_names) {
  using Signature = decltype(std::function{function});
  insert(
      detail::makeCommand<0>(
          name, help, parameter_names, static_cast<const Signature *>(nullptr),
          std::decay_t<Function>(std::forward<Function>(function))));
}

template <size_t Defaults, typename Result, typename... Params, typename Call>
void CommandRegistry::addFunction(
    std::string_view name, std::string_view help, std::initializer_list<std::string_view> parameter_names,
    Result (* /* signature */)(Params...), Call call) {
  insert(
      detail::makeCommand<Defaults>(
          name, help, parameter_names, static_cast<const std::function<Result(Params...)> *>(nullptr),
          std::move(call)));
}

template <size_t Defaults, typename Class, typename Result, typename... Params, typename Call>
void CommandRegistry::addMember(
    std::string_view name, std::string_view help, std::initializer_list<std::string_view> parameter_names,
    Result (Class::* /* signature */)(Params...), Call call) {
  addOnApp<Defaults, Class>(
      name, help, parameter_names, static_cast<const std::function<Result(Params...)> *>(nullptr), std::move(call));
}

template <size_t Defaults, typename Class, typename Result, typename... Params, typename Call>
void CommandRegistry::addMember(
    std::string_view name, std::string_view help, std::initializer_list<std::string_view> parameter_names,
    Result (Class::* /* signature */)(Params...) const, Call call) {
  addOnApp<Defaults, Class>(
      name, help, parameter_names, static_cast<const std::function<Result(Params...)> *>(nullptr), std::move(call));
}

template <size_t Defaults, typename Class, typename Signature, typename Call>
void CommandRegistry::addOnApp(
    std::string_view name, std::string_view help, std::initializer_list<std::string_view> parameter_names,
    const Signature *signature, Call call) {
  if constexpr (std::derived_from<Class, App>) {
    if (auto *app = dynamic_cast<Class *>(app_)) {
      insert(
          detail::makeCommand<Defaults>(
              name, help, parameter_names, signature,
              [app, call = std::move(call)](auto &&...arguments) -> decltype(auto) {
                return call(*app, std::forward<decltype(arguments)>(arguments)...);
              }));
    }
  } else {
    static_assert(
        detail::kUnsupported<Class>,
        "A [[command]] member must be static, or belong to the game's App class. Use "
        "Console::addCommand to run it on another object.");
  }
}

} // namespace strafe::console

namespace strafe::generated {

// Defined by the codegen for each target, with its [[command]] functions.
// Application calls the engine's, then the app's, at startup.
void registerEngineCommands(console::CommandRegistry &registry);
void registerAppCommands(console::CommandRegistry &registry);

} // namespace strafe::generated
