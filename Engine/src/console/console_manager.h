#pragma once

#include "strafe/console/command.h"
#include "strafe/core/log.h"

#include <array>
#include <cstddef>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

struct ImGuiInputTextCallbackData;

namespace strafe::input {
class InputManager;
} // namespace strafe::input

namespace strafe::console {

struct Line {
  log::Level level = log::Level::Info;
  // Command output, which has no level or location.
  bool plain = false;
  double seconds = 0.0;
  std::string location;
  std::string text;
};

// The state behind Console. Application owns it and drives its frame.
class ConsoleManager {
public:
  ConsoleManager() = default;
  ConsoleManager(const ConsoleManager &) = delete;
  ConsoleManager &operator=(const ConsoleManager &) = delete;

  // Collects the log from here on, so it comes before anything that logs.
  void initialize(input::InputManager &input);
  void shutdown();

  CommandRegistry &commands() { return commands_; }

  bool isOpen() const { return open_; }
  void setOpen(bool open);

  void submit(std::string_view line);
  void clear();

  // Runs the submitted lines and takes in what was logged.
  void update();
  // Inside the ImGui frame, after the game's windows.
  void draw();

private:
  static constexpr size_t kMaxLines = 4096;
  static constexpr size_t kMaxHistory = 128;

  static void onLog(void *user, const log::Record &record);
  static int onInputText(ImGuiInputTextCallbackData *data);

  void collect();
  void drawLog(float height);
  void drawInput();
  void drawDropdown();
  void drawSignature(const Completion &completion);
  void drawCandidates(const Completion &completion, float width);

  // What completes the text up to the cursor.
  Completion completion() const;
  // Whether the dropdown has anything to show.
  bool isDropdownVisible(const Completion &completion) const;
  void closeDropdown();
  void setText(std::string_view text, size_t cursor);
  // Replaces the word at the cursor, with the InputText inactive.
  void accept(const Completion &completion, size_t index);

  void submitInput();
  void completeInput(ImGuiInputTextCallbackData &data);
  void moveSelection(int step, size_t count);
  void browseHistory(ImGuiInputTextCallbackData &data);

  input::InputManager *input_ = nullptr;
  CommandRegistry commands_;
  std::vector<std::string> submitted_;

  // Logged on any thread, until the main thread collects it.
  std::mutex pending_mutex_;
  std::deque<Line> pending_;
  std::deque<Line> lines_;

  bool open_ = false;
  // 0 when fully up, 1 when fully down.
  float slide_ = 0.0f;
  bool focus_input_ = false;
  // Puts the cursor back where it was once the input is active again.
  bool restore_cursor_ = false;
  bool scroll_to_bottom_ = true;
  // The line the context menu was opened on.
  std::string context_text_;

  std::array<char, 1024> text_{};
  size_t cursor_ = 0;
  // text_ as of the last frame, to notice typing.
  std::string last_text_;

  // Typing opens the dropdown, and Escape or the history closes it.
  bool dropdown_open_ = false;
  size_t selected_ = 0;
  // Moved with the arrow keys, so Enter accepts rather than runs the line.
  bool navigated_ = false;
  bool scroll_to_selected_ = false;

  std::vector<std::string> history_;
  // Into history_, which is also its size when not browsing.
  size_t history_position_ = 0;
  // What was typed before browsing.
  std::string draft_;
};

} // namespace strafe::console
