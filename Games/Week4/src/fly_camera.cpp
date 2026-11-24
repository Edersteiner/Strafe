#include "fly_camera.h"

#include "strafe/input.h"

#include <algorithm>
#include <cmath>
#include <numbers>

using namespace strafe;

namespace {

constexpr float kMaxPitch = radians(89.0f);

constexpr Vec3 kWorldUp(0.0f, 1.0f, 0.0f);

} // namespace

void FlyCamera::bindActions() {
  using input::AxisDirection;
  using input::GamepadAxis;
  using input::GamepadButton;
  using input::Key;

  Input::bind("MoveForward", {Key::W, GamepadAxis::LeftY}, AxisDirection::Negative);
  Input::bind("MoveBack", {Key::S, GamepadAxis::LeftY});
  Input::bind("MoveLeft", {Key::A, GamepadAxis::LeftX}, AxisDirection::Negative);
  Input::bind("MoveRight", {Key::D, GamepadAxis::LeftX});
  Input::bind("MoveUp", {Key::Space, GamepadButton::RightShoulder});
  Input::bind("MoveDown", {Key::LeftCtrl, GamepadButton::LeftShoulder});
  Input::bind("Boost", {Key::LeftShift, GamepadButton::LeftStick});

  // The mouse looks through mouseDelta, we only bind the stick here.
  Input::bind("LookLeft", {GamepadAxis::RightX}, AxisDirection::Negative);
  Input::bind("LookRight", {GamepadAxis::RightX});
  Input::bind("LookUp", {GamepadAxis::RightY}, AxisDirection::Negative);
  Input::bind("LookDown", {GamepadAxis::RightY});
}

void FlyCamera::lookAt(Vec3 target) {
  const Vec3 forward = normalize(target - position);
  yaw = std::atan2(-forward.x, -forward.z);
  pitch = std::clamp(std::asin(forward.y), -kMaxPitch, kMaxPitch);
}

void FlyCamera::update(uint8_t player, float delta_seconds, bool mouse_look) {
  const Vec2 stick = Input::getVector("LookRight", "LookLeft", "LookUp", "LookDown", player);
  yaw -= stick.x * stick_turn_speed * delta_seconds;
  pitch += stick.y * stick_turn_speed * delta_seconds;
  if (mouse_look) {
    const Vec2 mouse = Input::mouseDelta(player);
    yaw -= mouse.x * mouse_sensitivity;
    pitch -= mouse.y * mouse_sensitivity;
  }
  pitch = std::clamp(pitch, -kMaxPitch, kMaxPitch);
  yaw = std::remainder(yaw, 2.0f * std::numbers::pi_v<float>);

  const Quat orientation = rotation();
  const Vec3 forward = orientation * Vec3(0.0f, 0.0f, -1.0f);
  const Vec3 right = orientation * Vec3(1.0f, 0.0f, 0.0f);

  const Vec2 move = Input::getVector("MoveRight", "MoveLeft", "MoveForward", "MoveBack", player);
  const float rise = Input::getActionAxisValue("MoveUp", player) - Input::getActionAxisValue("MoveDown", player);
  const float speed = move_speed * (Input::isActionPressed("Boost", player) ? boost_multiplier : 1.0f);
  position += (forward * move.y + right * move.x + kWorldUp * rise) * speed * delta_seconds;
}

Quat FlyCamera::rotation() const {
  return Quat::fromAxisAngle(kWorldUp, yaw) * Quat::fromAxisAngle(Vec3(1.0f, 0.0f, 0.0f), pitch);
}
