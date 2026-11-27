#include "console/builtin_commands.h"

#include "strafe/console.h"
#include "strafe/core/type_registry.h"
#include "strafe/ecs/components.h"
#include "strafe/ecs/registry.h"
#include "strafe/engine.h"
#include "strafe/input.h"
#include "strafe/world.h"

#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <variant>

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

} // namespace strafe::console
