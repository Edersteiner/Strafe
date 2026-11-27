#include "input/input_manager.h"

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

void InputManager::beginFrame() {
  mouse_delta_ = {};
  keys_pressed_this_frame_ = {};
  mouse_buttons_pressed_this_frame_ = {};
  for (Gamepad &gamepad : gamepads_) {
    gamepad.buttons_pressed_this_frame = {};
  }
}

void InputManager::handleEvents(const SDL_Event &event) {
  static_assert(kKeyCount == SDL_SCANCODE_COUNT);
  switch (event.type) {
  case SDL_EVENT_KEY_DOWN:
  case SDL_EVENT_KEY_UP:
    // A repeat isn't a press. Its real press may have gone to the UI.
    if (event.key.scancode < kKeyCount && !event.key.repeat) {
      keys_[event.key.scancode] = event.key.down;
      keys_pressed_this_frame_[event.key.scancode] |= event.key.down;
    }
    break;
  case SDL_EVENT_MOUSE_BUTTON_DOWN:
  case SDL_EVENT_MOUSE_BUTTON_UP:
    if (event.button.button < kMouseButtonCount) {
      mouse_buttons_[event.button.button] = event.button.down;
      mouse_buttons_pressed_this_frame_[event.button.button] |= event.button.down;
    }
    break;
  case SDL_EVENT_MOUSE_MOTION:
    // Summed, since a fast mouse sends several motion events per frame.
    mouse_delta_ += Vec2(event.motion.xrel, event.motion.yrel);
    break;
  // Gamepad buttons are polled, so only the down events are needed, for the latch.
  case SDL_EVENT_GAMEPAD_BUTTON_DOWN: {
    const auto it = std::ranges::find(gamepads_, event.gbutton.which, &Gamepad::id);
    if (it != gamepads_.end() && event.gbutton.button < kGamepadButtonCount) {
      it->buttons_pressed_this_frame[event.gbutton.button] = true;
    }
    break;
  }
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
  if (blocked_) {
    mouse_delta_ = {};
  }
  pollGamepads();
  updateActions();
}

void InputManager::pollGamepads() {
  static_assert(kGamepadButtonCount == SDL_GAMEPAD_BUTTON_COUNT);
  static_assert(kGamepadAxisCount == SDL_GAMEPAD_AXIS_COUNT);
  for (Gamepad &gamepad : gamepads_) {
    for (size_t i = 0; i < kGamepadButtonCount; ++i) {
      gamepad.buttons[i] = gamepad.buttons_pressed_this_frame[i] ||
          SDL_GetGamepadButton(gamepad.handle, static_cast<SDL_GamepadButton>(i));
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
      state.axis_value = isActive(name) ? readAction(action, player) : 0.0f;
      state.pressed = state.axis_value >= kPressThreshold;
      state.just_pressed = state.pressed && !was_pressed;
      state.just_released = !state.pressed && was_pressed;
    }
  }
}

float InputManager::readAction(const InputAction &action, uint8_t player) const {
  float value = 0.0f;
  for (const InputBinding &binding : action.bindings) {
    value = std::max(value, readBinding(binding, player));
  }
  return value;
}

float InputManager::readBinding(const InputBinding &binding, uint8_t player) const {
  return std::visit(
      [&](auto source) -> float {
        using Source = decltype(source);
        const auto index = static_cast<size_t>(source);
        if constexpr (std::is_same_v<Source, Key>) {
          const bool down = index < kKeyCount && (keys_[index] || keys_pressed_this_frame_[index]);
          return player == kKeyboardMousePlayer && down ? 1.0f : 0.0f;
        } else if constexpr (std::is_same_v<Source, MouseButton>) {
          const bool down =
              index < kMouseButtonCount && (mouse_buttons_[index] || mouse_buttons_pressed_this_frame_[index]);
          return player == kKeyboardMousePlayer && down ? 1.0f : 0.0f;
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

  // Rebaselines every player under the new owners, so an input held while it
  // changes hands fires no edges on the next update.
  for (auto &[name, action] : actions_) {
    rebaseline(name, action);
  }
}

void InputManager::rebaseline(std::string_view action_name, InputAction &action) const {
  for (uint8_t player = 0; player < kMaxPlayers; ++player) {
    InputAction::PlayerState &state = action.players[player];
    state.axis_value = isActive(action_name) ? readAction(action, player) : 0.0f;
    state.pressed = state.axis_value >= kPressThreshold;
  }
}

bool InputManager::isActive(std::string_view action_name) const {
  return !blocked_ || action_name.starts_with(kEngineActionPrefix);
}

void InputManager::setBlocked(bool blocked) {
  if (blocked_ == blocked) {
    return;
  }
  blocked_ = blocked;
  for (auto &[name, action] : actions_) {
    if (name.starts_with(kEngineActionPrefix)) {
      continue;
    }
    if (!blocked) {
      rebaseline(name, action);
      continue;
    }
    // Released right away, since the game may read actions before the next
    // update. A press from this frame was never seen, so it isn't released.
    for (InputAction::PlayerState &state : action.players) {
      const bool was_held = state.pressed && !state.just_pressed;
      state = {.just_released = was_held || state.just_released};
    }
  }
  mouse_delta_ = {};
  applyMouseMode();
}

void InputManager::applyMouseMode() {
  const bool enabled = wants_relative_mouse_ && !blocked_;
  if (window_ && !SDL_SetWindowRelativeMouseMode(window_, enabled)) {
    log::warn("Failed to set relative mouse mode: {}", SDL_GetError());
  }
}

bool InputManager::isMouseCaptured() const { return window_ && SDL_GetWindowRelativeMouseMode(window_); }

bool InputManager::pressesEngineAction(const SDL_Event &event) const {
  InputSource source;
  if (event.type == SDL_EVENT_KEY_DOWN) {
    source = static_cast<Key>(event.key.scancode);
  } else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
    source = static_cast<MouseButton>(event.button.button);
  } else {
    return false;
  }
  return std::ranges::any_of(actions_, [&](const auto &entry) {
    return entry.first.starts_with(kEngineActionPrefix) &&
        std::ranges::find(entry.second.bindings, source, &InputBinding::source) != entry.second.bindings.end();
  });
}

bool InputManager::isEngineActionJustPressed(std::string_view action_name) const {
  const auto it = actions_.find(action_name);
  return it != actions_.end() && std::ranges::any_of(it->second.players, &InputAction::PlayerState::just_pressed);
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

namespace strafe {

input::InputManager *Input::manager_ = nullptr;

input::InputManager &Input::manager() {
  assert(manager_ && "Input is only usable from App::onInit until App::onShutdown returns");
  return *manager_;
}

void Input::bind(
    std::string_view action_name, std::initializer_list<input::InputSource> bindings,
    input::AxisDirection axis_direction) {
  auto &actions = manager().actions_;
  auto it = actions.find(action_name);
  if (it == actions.end()) {
    it = actions.emplace(std::string(action_name), input::InputAction{}).first;
  }
  for (const input::InputSource &source : bindings) {
    it->second.bindings.push_back({.source = source, .axis_direction = axis_direction});
  }
}

void Input::rebind(
    std::string_view action_name, std::initializer_list<input::InputSource> bindings,
    input::AxisDirection axis_direction) {
  auto &actions = manager().actions_;
  if (const auto it = actions.find(action_name); it != actions.end()) {
    it->second.bindings.clear();
  }
  bind(action_name, bindings, axis_direction);
}

void Input::unbind(std::string_view action_name, input::InputSource source) {
  auto &actions = manager().actions_;
  if (const auto it = actions.find(action_name); it != actions.end()) {
    std::erase_if(it->second.bindings, [&](const input::InputBinding &binding) { return binding.source == source; });
  }
}

void Input::unbind(std::string_view action_name) {
  auto &actions = manager().actions_;
  if (const auto it = actions.find(action_name); it != actions.end()) {
    it->second.bindings.clear();
  }
}

void Input::removeAction(std::string_view action_name) {
  auto &actions = manager().actions_;
  if (const auto it = actions.find(action_name); it != actions.end()) {
    actions.erase(it);
  }
}

bool Input::hasAction(std::string_view action_name) { return manager().actions_.contains(action_name); }

std::vector<std::string> Input::actionNames() {
  std::vector<std::string> names;
  names.reserve(manager().actions_.size());
  for (const auto &[name, action] : manager().actions_) {
    names.push_back(name);
  }
  std::ranges::sort(names);
  return names;
}

std::vector<input::InputBinding> Input::bindings(std::string_view action_name) {
  const auto &actions = manager().actions_;
  const auto it = actions.find(action_name);
  return it != actions.end() ? it->second.bindings : std::vector<input::InputBinding>{};
}

void Input::setPlayerCount(uint8_t count) {
  assert(count >= 1 && count <= input::kMaxPlayers && "player count out of range");
  input::InputManager &state = manager();
  state.player_count_ = std::clamp<uint8_t>(count, 1, input::kMaxPlayers);
  state.assignDevices();
}

uint8_t Input::playerCount() { return manager().player_count_; }

bool Input::isActionPressed(std::string_view action_name, uint8_t player_id) {
  return manager().anyPlayer(
      action_name, player_id, [](const input::InputAction::PlayerState &state) { return state.pressed; });
}

bool Input::isActionJustPressed(std::string_view action_name, uint8_t player_id) {
  return manager().anyPlayer(
      action_name, player_id, [](const input::InputAction::PlayerState &state) { return state.just_pressed; });
}

bool Input::isActionJustReleased(std::string_view action_name, uint8_t player_id) {
  return manager().anyPlayer(
      action_name, player_id, [](const input::InputAction::PlayerState &state) { return state.just_released; });
}

float Input::getActionAxisValue(std::string_view action_name, uint8_t player_id) {
  const input::InputAction *action = manager().findAction(action_name);
  if (!action) {
    return 0.0f;
  }
  if (player_id != input::kAnyPlayer) {
    return player_id < input::kMaxPlayers ? action->players[player_id].axis_value : 0.0f;
  }
  float value = 0.0f;
  for (const input::InputAction::PlayerState &state : action->players) {
    value = std::max(value, state.axis_value);
  }
  return value;
}

Vec2 Input::getVector(
    std::string_view positive_x, std::string_view negative_x, std::string_view positive_y, std::string_view negative_y,
    uint8_t player_id) {
  const Vec2 vector(
      getActionAxisValue(positive_x, player_id) - getActionAxisValue(negative_x, player_id),
      getActionAxisValue(positive_y, player_id) - getActionAxisValue(negative_y, player_id));
  return length(vector) > 1.0f ? normalize(vector) : vector;
}

Vec2 Input::mouseDelta(uint8_t player_id) {
  const input::InputManager &state = manager();
  const bool owns_mouse = player_id == input::kAnyPlayer || player_id == input::InputManager::kKeyboardMousePlayer;
  return owns_mouse ? state.mouse_delta_ : Vec2{};
}

void Input::setRelativeMouseMode(bool enabled) {
  input::InputManager &state = manager();
  state.wants_relative_mouse_ = enabled;
  state.applyMouseMode();
}

bool Input::isRelativeMouseMode() { return manager().wants_relative_mouse_; }

} // namespace strafe
