#pragma once

#include "strafe/console/command.h"
#include "strafe/core/log.h"
#include "strafe/renderer.h"

#include <optional>

namespace strafe::console {

// Lists every command, or explains one.
[[command]] void help(const std::optional<CommandName> &command);

// Removes every line from the console.
[[command]] void clear();

// Prints text.
[[command]] void echo(const RestOfLine &text);

// Quits the game.
[[command]] void quit();

// Shows or sets the lowest level that is logged.
[[command]] void logLevel(std::optional<log::Level> level);

// Shows or sets what the renderer draws instead of the lit scene.
[[command]] void debugView(std::optional<renderer::DebugView> view);

// Lists the input actions and what they are bound to.
[[command]] void actions();

// Lists the reflected types.
[[command]] void types();

} // namespace strafe::console
