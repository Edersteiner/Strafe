#pragma once

#include "strafe/console/command.h"
#include "strafe/core/log.h"
#include "strafe/ecs/entity.h"
#include "strafe/renderer.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace strafe::console {

// The name of a system, completed from the schedule.
struct SystemName {
  std::string name;
};

template <> struct Argument<SystemName> {
  static constexpr std::string_view kType = "system";
  static constexpr size_t kWords = 1;
  static bool read(ArgumentReader &in, SystemName &value);
  static std::vector<std::string> choices();
};

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

// Lists the entities with their names and components.
[[command]] void entities();

// Destroys an entity and its components.
[[command]] void destroy(ecs::Entity entity);

// Lists the systems of each stage in the order they run.
[[command]] void systems();

// Shows whether a system runs, or turns it on or off.
[[command("system")]] void systemEnabled(const SystemName &name, std::optional<bool> enabled);

} // namespace strafe::console
