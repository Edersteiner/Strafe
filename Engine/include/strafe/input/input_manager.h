#pragma once

#include "input_codes.h"

#include "strafe/core/math.h"

#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace strafe::input {

using InputSource = std::variant<Key, MouseButton, GamepadButton, GamepadAxis>;

struct SDL_Event;

struct InputAction {
  std::vector<InputSource> bindings;
  bool pressed = false;
  bool just_pressed = false;
  bool just_released = false;
  float axis_value = 0.0f;
};

class InputManager {
public:
  // Creates or updates a binding for an action.
  void bind(
      std::string_view action_name, std::span<InputSource> bindings,
      AxisDirection axis_direction = AxisDirection::Positive);
  // overload for a single binding.
  void bind(std::string_view action_name, InputSource binding, AxisDirection axis_direction = AxisDirection::Positive) {
    bind(action_name, std::span<InputSource>(&binding, 1), axis_direction);
  };

  bool isActionPressed(std::string_view action_name, uint8_t player_id = kAnyPlayer) const;
  bool isActionJustPressed(std::string_view action_name, uint8_t player_id = kAnyPlayer) const;
  bool isActionJustReleased(std::string_view &action_name, uint8_t player_id = kAnyPlayer) const;

  float getActionAxisValue(const std::string_view &action_name, uint8_t player_id = kAnyPlayer) const;
  Vec2 getVector(
      std::string_view positive_x, std::string_view negative_x, std::string_view positive_y,
      std::string_view negative_y, uint8_t player_id = kAnyPlayer) const;

private:
  static constexpr uint8_t kAnyPlayer = UINT8_MAX;

  void handleEvents(const SDL_Event &events);

  std::unordered_map<std::string, InputAction> actions_;
};
} // namespace strafe::input
