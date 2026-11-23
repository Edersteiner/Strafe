#pragma once

#include "input_codes.h"

#include "strafe/core/math.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

union SDL_Event;
struct SDL_Gamepad;
struct SDL_Window;

namespace strafe {
class Application;
} // namespace strafe

namespace strafe::input {

inline constexpr uint8_t kMaxPlayers = 4;
inline constexpr uint8_t kAnyPlayer = UINT8_MAX;

using InputSource = std::variant<Key, MouseButton, GamepadButton, GamepadAxis>;

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

// Named actions on top of the keyboard, mouse and gamepads.
class InputManager {
public:
  InputManager() = default;
  InputManager(const InputManager &) = delete;
  InputManager &operator=(const InputManager &) = delete;

  // Adds the bindings to the action, creating it on first use.
  void bind(
      std::string_view action_name, std::initializer_list<InputSource> bindings,
      AxisDirection axis_direction = AxisDirection::Positive);

  // Keyboard and mouse go to player 0, gamepads to players 1 and up in
  // connection order. With one player, player 0 owns every device.
  void setPlayerCount(uint8_t count);
  uint8_t playerCount() const { return player_count_; }

  // Each query reads that player's devices, or every player's by default.
  bool isActionPressed(std::string_view action_name, uint8_t player_id = kAnyPlayer) const;
  bool isActionJustPressed(std::string_view action_name, uint8_t player_id = kAnyPlayer) const;
  bool isActionJustReleased(std::string_view action_name, uint8_t player_id = kAnyPlayer) const;

  // 0 to 1. Keys and buttons give 0 or 1, axes how far past the deadzone.
  float getActionAxisValue(std::string_view action_name, uint8_t player_id = kAnyPlayer) const;
  // Positive minus negative on each axis, clamped to length 1. Diagonals on
  // keys don't move faster, and a half tilted stick stays at half.
  Vec2 getVector(
      std::string_view positive_x, std::string_view negative_x, std::string_view positive_y,
      std::string_view negative_y, uint8_t player_id = kAnyPlayer) const;

  // Pixels moved this frame, +y down. A distance, so don't scale it by delta
  // time. Zero for players who don't own the mouse.
  Vec2 mouseDelta(uint8_t player_id = kAnyPlayer) const;
  // Hides the cursor and keeps reporting motion past the window's edges.
  void setRelativeMouseMode(bool enabled);

private:
  friend class strafe::Application;

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
  };

  // Lets the maps below be searched with a string_view without building a std::string.
  struct NameHash {
    using is_transparent = void; // NOLINT(readability-identifier-naming): the standard library's name.
    size_t operator()(std::string_view name) const { return std::hash<std::string_view>{}(name); }
  };

  void initialize(SDL_Window *window);
  void shutdown();

  void beginFrame();
  void handleEvents(const SDL_Event &event);
  void update();

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
  Vec2 mouse_delta_;
  std::vector<Gamepad> gamepads_;
};

} // namespace strafe::input
