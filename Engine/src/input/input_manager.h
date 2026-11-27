#pragma once

#include "strafe/core/math.h"
#include "strafe/input.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

union SDL_Event;
struct SDL_Gamepad;
struct SDL_Window;

namespace strafe::input {

struct InputAction {
  struct PlayerState {
    // 0 to 1, the strongest of the player's bindings.
    float axis_value = 0.0f;
    bool pressed = false;
    bool just_pressed = false;
    bool just_released = false;
  };

  std::vector<InputBinding> bindings;
  std::array<PlayerState, kMaxPlayers> players{};
};

// The state behind Input. Application owns it and drives its frame, and the
// queries live on Input.
class InputManager {
public:
  InputManager() = default;
  InputManager(const InputManager &) = delete;
  InputManager &operator=(const InputManager &) = delete;

  void initialize(SDL_Window *window);
  void shutdown();

  void beginFrame();
  void handleEvents(const SDL_Event &event);
  void update();

  // While blocked, every action but the engine's reads as released and the
  // mouse is free. Inputs still held when it ends don't count as new presses.
  void setBlocked(bool blocked);
  // Whether the window is in relative mouse mode right now.
  bool isMouseCaptured() const;

  // Whether the event presses something bound to an engine action.
  bool pressesEngineAction(const SDL_Event &event) const;
  // False for an action the game removed.
  bool isEngineActionJustPressed(std::string_view action_name) const;

private:
  friend class strafe::Input;

  static constexpr uint8_t kNoPlayer = UINT8_MAX - 1;
  static constexpr uint8_t kKeyboardMousePlayer = 0;

  static constexpr size_t kKeyCount = 512;
  // SDL numbers mouse buttons from 1, so index 0 stays unused.
  static constexpr size_t kMouseButtonCount = 6;
  static constexpr size_t kGamepadButtonCount = 26;
  static constexpr size_t kGamepadAxisCount = 6;

  struct Gamepad {
    SDL_Gamepad *handle = nullptr;
    uint32_t id = 0;
    uint8_t player = kNoPlayer;
    std::array<float, kGamepadAxisCount> axes{};
    std::array<bool, kGamepadButtonCount> buttons{};
    std::array<bool, kGamepadButtonCount> buttons_pressed_this_frame{};
  };

  // Lets the maps below be searched with a string_view without building a std::string.
  struct NameHash {
    using is_transparent = void; // NOLINT(readability-identifier-naming): the standard library's name.
    size_t operator()(std::string_view name) const { return std::hash<std::string_view>{}(name); }
  };

  void pollGamepads();
  void updateActions();
  void addGamepad(uint32_t id);
  void removeGamepad(uint32_t id);
  void assignDevices();
  void applyMouseMode();
  bool isActive(std::string_view action_name) const;
  // Takes the current inputs as the previous ones, so no edges fire for them.
  void rebaseline(std::string_view action_name, InputAction &action) const;
  float readBinding(const InputBinding &binding, uint8_t player) const;
  float readAction(const InputAction &action, uint8_t player) const;
  const InputAction *findAction(std::string_view action_name) const;
  template <typename Predicate>
  bool anyPlayer(std::string_view action_name, uint8_t player_id, Predicate predicate) const;

  SDL_Window *window_ = nullptr;
  bool blocked_ = false;
  bool wants_relative_mouse_ = false;
  std::unordered_map<std::string, InputAction, NameHash, std::equal_to<>> actions_;
  mutable std::unordered_set<std::string, NameHash, std::equal_to<>> warned_names_;
  uint8_t player_count_ = 1;

  std::array<bool, kKeyCount> keys_{};
  std::array<bool, kMouseButtonCount> mouse_buttons_{};
  std::array<bool, kKeyCount> keys_pressed_this_frame_{};
  std::array<bool, kMouseButtonCount> mouse_buttons_pressed_this_frame_{};
  Vec2 mouse_delta_;
  std::vector<Gamepad> gamepads_;
};

} // namespace strafe::input
