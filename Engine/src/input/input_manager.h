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

struct InputBinding {
  InputSource source;
  // Which half of an axis counts. Keys and buttons ignore it.
  AxisDirection axis_direction = AxisDirection::Positive;
};

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
  float readBinding(const InputBinding &binding, uint8_t player) const;
  float readAction(const InputAction &action, uint8_t player) const;
  const InputAction *findAction(std::string_view action_name) const;
  template <typename Predicate>
  bool anyPlayer(std::string_view action_name, uint8_t player_id, Predicate predicate) const;

  SDL_Window *window_ = nullptr;
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
