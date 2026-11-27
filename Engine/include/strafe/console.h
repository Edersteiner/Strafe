#pragma once

#include "strafe/console/command.h"
#include "strafe/core/log.h"

#include <format>
#include <initializer_list>
#include <string_view>
#include <utility>

namespace strafe {
class Application;
} // namespace strafe

namespace strafe::console {
class ConsoleManager;
} // namespace strafe::console

namespace strafe {

// The drop-down developer console.
//
// [[command]] on a function in a header adds it as a command, with the comment
// above it as help. It can be free, static, or a member of the game's App.
// addCommand is for anything else.
class Console {
public:
  Console() = delete;

  // While it is open, every action but the engine's reads as released.
  static bool isOpen();
  static void open();
  static void close();

  // Runs the line at the start of the next frame, before App::onUpdate.
  static void execute(std::string_view line);
  static void clear();

  // Adds a command that calls function, replacing one with the same name.
  //   Console::addCommand("scene", "Loads a scene.", [this](int index) { loadScene(index); }, {"index"});
  template <typename Function>
  static void addCommand(
      std::string_view name, std::string_view help, Function &&function,
      std::initializer_list<std::string_view> parameter_names = {}) {
    registry().add(name, help, std::forward<Function>(function), parameter_names);
  }
  static void removeCommand(std::string_view name);
  static const console::CommandRegistry &commands();

  // Shows in the console and the log, whatever the log level.
  template <typename... Args> static void print(std::format_string<Args...> fmt, Args &&...args) {
    log::print(fmt, std::forward<Args>(args)...);
  }

private:
  friend class Application;

  static console::ConsoleManager &manager();
  static console::CommandRegistry &registry();

  // Set by Application while the app runs.
  static console::ConsoleManager *manager_;
};

} // namespace strafe
