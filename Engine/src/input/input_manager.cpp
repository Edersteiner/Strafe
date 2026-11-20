#include "strafe/input/input_manager.h"

#include "strafe/core/log.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_mouse.h>

#include <algorithm>
#include <cassert>
#include <type_traits>

namespace strafe::input {

// The code enums cast straight to SDL's values.
// Ensure that the mapping doesnt get broken by checking
// the first and last real codes in each category.
static_assert(static_cast<int>(Key::A) == SDL_SCANCODE_A);
static_assert(static_cast<int>(Key::EndCall) == SDL_SCANCODE_ENDCALL);
static_assert(static_cast<int>(MouseButton::Left) == SDL_BUTTON_LEFT);
static_assert(static_cast<int>(MouseButton::X2) == SDL_BUTTON_X2);
static_assert(static_cast<int>(GamepadButton::South) == SDL_GAMEPAD_BUTTON_SOUTH);
static_assert(static_cast<int>(GamepadButton::Misc6) == SDL_GAMEPAD_BUTTON_COUNT - 1);
static_assert(static_cast<int>(GamepadAxis::LeftX) == SDL_GAMEPAD_AXIS_LEFTX);
static_assert(static_cast<int>(GamepadAxis::RightTrigger) == SDL_GAMEPAD_AXIS_COUNT - 1);

namespace {

// Pressed from half strength, so a half pulled trigger counts.
constexpr float kPressThreshold = 0.5f;
// Sticks rest slightly off center, and that drift must not move anything.
constexpr float kStickDeadzone = 0.2f;
constexpr float kTriggerDeadzone = 0.05f;

float normalizeAxis(Sint16 raw) { return std::clamp(static_cast<float>(raw) / 32767.0f, -1.0f, 1.0f); }

// Remaps [deadzone, 1] to [0, 1], so movement starts smoothly at the edge.
float remapPastDeadzone(float value, float deadzone) {
  if (value <= deadzone) {
    return 0.0f;
  }
  return std::min((value - deadzone) / (1.0f - deadzone), 1.0f);
}

Vec2 applyRadialDeadzone(Vec2 stick) {
  const float stick_length = length(stick);
  if (stick_length <= kStickDeadzone) {
    return {};
  }
  return stick / stick_length * remapPastDeadzone(stick_length, kStickDeadzone);
}

} // namespace

void InputManager::initialize(SDL_Window *window) { window_ = window; }

void InputManager::bind(
    std::string_view action_name, std::initializer_list<InputSource> bindings, AxisDirection axis_direction) {
  auto it = actions_.find(action_name);
  if (it == actions_.end()) {
    it = actions_.emplace(std::string(action_name), InputAction{}).first;
  }
  for (const InputSource &source : bindings) {
    it->second.bindings.push_back({.source = source, .axis_direction = axis_direction});
  }
}

void InputManager::setPlayerCount(uint8_t count) {
  assert(count >= 1 && count <= kMaxPlayers && "player count out of range");
  player_count_ = std::clamp<uint8_t>(count, 1, kMaxPlayers);
  assignDevices();
}

template <typename Predicate>
bool InputManager::anyPlayer(std::string_view action_name, uint8_t player_id, Predicate predicate) const {
  const InputAction *action = findAction(action_name);
  if (!action) {
    return false;
  }
  if (player_id != kAnyPlayer) {
    return player_id < kMaxPlayers && predicate(action->players[player_id]);
  }
  return std::ranges::any_of(action->players, predicate);
}

bool InputManager::isActionPressed(std::string_view action_name, uint8_t player_id) const {
  return anyPlayer(action_name, player_id, [](const InputAction::PlayerState &state) { return state.pressed; });
}

bool InputManager::isActionJustPressed(std::string_view action_name, uint8_t player_id) const {
  return anyPlayer(action_name, player_id, [](const InputAction::PlayerState &state) { return state.just_pressed; });
}

bool InputManager::isActionJustReleased(std::string_view action_name, uint8_t player_id) const {
  return anyPlayer(action_name, player_id, [](const InputAction::PlayerState &state) { return state.just_released; });
}

float InputManager::getActionAxisValue(std::string_view action_name, uint8_t player_id) const {
  const InputAction *action = findAction(action_name);
  if (!action) {
    return 0.0f;
  }
  if (player_id != kAnyPlayer) {
    return player_id < kMaxPlayers ? action->players[player_id].axis_value : 0.0f;
  }
  float value = 0.0f;
  for (const InputAction::PlayerState &state : action->players) {
    value = std::max(value, state.axis_value);
  }
  return value;
}

Vec2 InputManager::getVector(
    std::string_view positive_x, std::string_view negative_x, std::string_view positive_y, std::string_view negative_y,
    uint8_t player_id) const {
  const Vec2 vector(
      getActionAxisValue(positive_x, player_id) - getActionAxisValue(negative_x, player_id),
      getActionAxisValue(positive_y, player_id) - getActionAxisValue(negative_y, player_id));
  return length(vector) > 1.0f ? normalize(vector) : vector;
}

Vec2 InputManager::mouseDelta(uint8_t player_id) const {
  return player_id == kAnyPlayer || player_id == kKeyboardMousePlayer ? mouse_delta_ : Vec2{};
}

void InputManager::setRelativeMouseMode(bool enabled) {
  if (window_ && !SDL_SetWindowRelativeMouseMode(window_, enabled)) {
    log::warn("Failed to set relative mouse mode: {}", SDL_GetError());
  }
}

void InputManager::beginFrame() { mouse_delta_ = {}; }

void InputManager::handleEvents(const SDL_Event &event) {
  static_assert(kKeyCount == SDL_SCANCODE_COUNT);
  switch (event.type) {
  case SDL_EVENT_KEY_DOWN:
  case SDL_EVENT_KEY_UP:
    if (event.key.scancode < kKeyCount) {
      keys_[event.key.scancode] = event.key.down;
    }
    break;
  case SDL_EVENT_MOUSE_BUTTON_DOWN:
  case SDL_EVENT_MOUSE_BUTTON_UP:
    if (event.button.button < kMouseButtonCount) {
      mouse_buttons_[event.button.button] = event.button.down;
    }
    break;
  case SDL_EVENT_MOUSE_MOTION:
    // Summed, since a fast mouse sends several motion events per frame.
    mouse_delta_ += Vec2(event.motion.xrel, event.motion.yrel);
    break;
  case SDL_EVENT_GAMEPAD_ADDED:
    addGamepad(event.gdevice.which);
    break;
  case SDL_EVENT_GAMEPAD_REMOVED:
    removeGamepad(event.gdevice.which);
    break;
  default:
    break;
  }
}

void InputManager::update() {
  pollGamepads();
  updateActions();
}

void InputManager::pollGamepads() {
  static_assert(kGamepadButtonCount == SDL_GAMEPAD_BUTTON_COUNT);
  static_assert(kGamepadAxisCount == SDL_GAMEPAD_AXIS_COUNT);
  for (Gamepad &gamepad : gamepads_) {
    for (size_t i = 0; i < kGamepadButtonCount; ++i) {
      gamepad.buttons[i] = SDL_GetGamepadButton(gamepad.handle, static_cast<SDL_GamepadButton>(i));
    }

    const auto read = [&](GamepadAxis axis) {
      return normalizeAxis(SDL_GetGamepadAxis(gamepad.handle, static_cast<SDL_GamepadAxis>(axis)));
    };
    const auto store = [&](GamepadAxis axis, float value) { gamepad.axes[static_cast<size_t>(axis)] = value; };

    const Vec2 left = applyRadialDeadzone({read(GamepadAxis::LeftX), read(GamepadAxis::LeftY)});
    const Vec2 right = applyRadialDeadzone({read(GamepadAxis::RightX), read(GamepadAxis::RightY)});
    store(GamepadAxis::LeftX, left.x);
    store(GamepadAxis::LeftY, left.y);
    store(GamepadAxis::RightX, right.x);
    store(GamepadAxis::RightY, right.y);
    store(GamepadAxis::LeftTrigger, remapPastDeadzone(read(GamepadAxis::LeftTrigger), kTriggerDeadzone));
    store(GamepadAxis::RightTrigger, remapPastDeadzone(read(GamepadAxis::RightTrigger), kTriggerDeadzone));
  }
}

void InputManager::updateActions() {
  // Every player slot updates, even past the player count, so its previous
  // state is current when the count grows and no stale edge fires.
  for (auto &[name, action] : actions_) {
    for (uint8_t player = 0; player < kMaxPlayers; ++player) {
      InputAction::PlayerState &state = action.players[player];
      const bool was_pressed = state.pressed;
      state.axis_value = 0.0f;
      for (const InputBinding &binding : action.bindings) {
        state.axis_value = std::max(state.axis_value, readBinding(binding, player));
      }
      state.pressed = state.axis_value >= kPressThreshold;
      state.just_pressed = state.pressed && !was_pressed;
      state.just_released = !state.pressed && was_pressed;
    }
  }
}

float InputManager::readBinding(const InputBinding &binding, uint8_t player) const {
  return std::visit(
      [&](auto source) -> float {
        using Source = decltype(source);
        const auto index = static_cast<size_t>(source);
        if constexpr (std::is_same_v<Source, Key>) {
          return player == kKeyboardMousePlayer && index < kKeyCount && keys_[index] ? 1.0f : 0.0f;
        } else if constexpr (std::is_same_v<Source, MouseButton>) {
          return player == kKeyboardMousePlayer && index < kMouseButtonCount && mouse_buttons_[index] ? 1.0f : 0.0f;
        } else {
          // The strongest of every pad this player owns.
          float value = 0.0f;
          for (const Gamepad &gamepad : gamepads_) {
            if (gamepad.player != player) {
              continue;
            }
            if constexpr (std::is_same_v<Source, GamepadButton>) {
              value = std::max(value, gamepad.buttons[index] ? 1.0f : 0.0f);
            } else {
              const float axis = gamepad.axes[index];
              value = std::max(value, binding.axis_direction == AxisDirection::Negative ? -axis : axis);
            }
          }
          return value;
        }
      },
      binding.source);
}

const InputAction *InputManager::findAction(std::string_view action_name) const {
  const auto it = actions_.find(action_name);
  if (it != actions_.end()) {
    return &it->second;
  }
  if (!warned_names_.contains(action_name)) {
    warned_names_.emplace(action_name);
    log::warn("Unknown input action '{}'", action_name);
  }
  return nullptr;
}

void InputManager::shutdown() {
  for (const Gamepad &gamepad : gamepads_) {
    SDL_CloseGamepad(gamepad.handle);
  }
  gamepads_.clear();
  window_ = nullptr;
}

void InputManager::assignDevices() {
  for (size_t i = 0; i < gamepads_.size(); ++i) {
    if (player_count_ == 1) {
      gamepads_[i].player = 0;
    } else {
      gamepads_[i].player = i + 1 < player_count_ ? static_cast<uint8_t>(i + 1) : kNoPlayer;
    }
  }
}

void InputManager::addGamepad(uint32_t id) {
  if (std::ranges::find(gamepads_, id, &Gamepad::id) != gamepads_.end()) {
    return;
  }
  SDL_Gamepad *handle = SDL_OpenGamepad(id);
  if (!handle) {
    log::warn("Failed to open gamepad: {}", SDL_GetError());
    return;
  }
  const char *name = SDL_GetGamepadName(handle);
  log::info("Gamepad connected: {}", name ? name : "unknown");
  gamepads_.push_back({.handle = handle, .id = id});
  assignDevices();
}

void InputManager::removeGamepad(uint32_t id) {
  const auto it = std::ranges::find(gamepads_, id, &Gamepad::id);
  if (it == gamepads_.end()) {
    return;
  }
  const char *name = SDL_GetGamepadName(it->handle);
  log::info("Gamepad disconnected: {}", name ? name : "unknown");
  SDL_CloseGamepad(it->handle);
  gamepads_.erase(it);
  assignDevices();
}

} // namespace strafe::input
