#pragma once

#include "strafe/core/math.h"

#include <cstdint>

namespace strafe::input {
class InputManager;
} // namespace strafe::input

struct FlyCamera {
  strafe::Vec3 position;
  float yaw = 0.0f;
  float pitch = 0.0f;

  float move_speed = 5.0f;
  float boost_multiplier = 4.0f;
  float stick_turn_speed = strafe::radians(150.0f);
  float mouse_sensitivity = 0.0025f;

  static void bindActions(strafe::input::InputManager &input);
  void lookAt(strafe::Vec3 target);
  void update(const strafe::input::InputManager &input, uint8_t player, float delta_seconds, bool mouse_look);
  strafe::Quat rotation() const;
};
