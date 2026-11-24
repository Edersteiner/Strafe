#pragma once

#include "strafe/core/types/vec2.h"
#include "strafe/input/input_codes.h"

#include <cstdint>
#include <initializer_list>
#include <string_view>
#include <variant>

namespace strafe {
class Application;
} // namespace strafe

namespace strafe::input {

class InputManager;

inline constexpr uint8_t kMaxPlayers = 4;
inline constexpr uint8_t kAnyPlayer = UINT8_MAX;

using InputSource = std::variant<Key, MouseButton, GamepadButton, GamepadAxis>;

} // namespace strafe::input

namespace strafe {

// Named actions on top of the keyboard, mouse and gamepads. Usable from
// App::onInit until App::onShutdown returns.
class Input {
public:
  Input() = delete;

  // Adds the bindings to the action, creating it on first use.
  static void bind(
      std::string_view action_name, std::initializer_list<input::InputSource> bindings,
      input::AxisDirection axis_direction = input::AxisDirection::Positive);

  // Keyboard and mouse go to player 0, gamepads to players 1 and up in
  // connection order. With one player, player 0 owns every device.
  static void setPlayerCount(uint8_t count);
  static uint8_t playerCount();

  // Each query reads that player's devices, or every player's by default.
  static bool isActionPressed(std::string_view action_name, uint8_t player_id = input::kAnyPlayer);
  static bool isActionJustPressed(std::string_view action_name, uint8_t player_id = input::kAnyPlayer);
  static bool isActionJustReleased(std::string_view action_name, uint8_t player_id = input::kAnyPlayer);

  // 0 to 1. Keys and buttons give 0 or 1, axes how far past the deadzone.
  static float getActionAxisValue(std::string_view action_name, uint8_t player_id = input::kAnyPlayer);
  // Positive minus negative on each axis, clamped to length 1. Diagonals on
  // keys don't move faster, and a half tilted stick stays at half.
  static Vec2 getVector(
      std::string_view positive_x, std::string_view negative_x, std::string_view positive_y,
      std::string_view negative_y, uint8_t player_id = input::kAnyPlayer);

  // Pixels moved this frame, +y down. A distance, so don't scale it by delta
  // time. Zero for players who don't own the mouse.
  static Vec2 mouseDelta(uint8_t player_id = input::kAnyPlayer);
  // Hides the cursor and keeps reporting motion past the window's edges.
  static void setRelativeMouseMode(bool enabled);
  static bool isRelativeMouseMode();

private:
  friend class Application;

  static input::InputManager &manager();

  // Set by Application while the app runs.
  static input::InputManager *manager_;
};

} // namespace strafe
