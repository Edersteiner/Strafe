#include "console/console_manager.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <format>

namespace strafe::console {

namespace {

constexpr float kHeight = 0.45f; // Of the window.
constexpr float kSlideSeconds = 0.12f;
constexpr size_t kMaxRows = 8;
constexpr float kMinDropdownWidth = 220.0f;
constexpr float kMaxDropdownWidth = 720.0f;
constexpr float kColumnGap = 20.0f;
// The x matches the window padding, so the input lines up with the log.
constexpr ImVec2 kInputPadding(8.0f, 6.0f);

// The theme's accent.
ImVec4 accentColor() { return ImGui::GetStyleColorVec4(ImGuiCol_CheckMark); }

float rowHeight() { return ImGui::GetTextLineHeight() + 6.0f; }

float inputHeight() { return ImGui::GetTextLineHeight() + 2.0f * kInputPadding.y; }

float textWidth(std::string_view text) { return ImGui::CalcTextSize(text.data(), text.data() + text.size()).x; }

ImVec4 lineColor(const Line &line) {
  if (line.plain) {
    return ImGui::GetStyleColorVec4(ImGuiCol_Text);
  }
  switch (line.level) {
  case log::Level::Error:
    return {0.95f, 0.39f, 0.39f, 1.0f};
  case log::Level::Warn:
    return {0.93f, 0.75f, 0.35f, 1.0f};
  case log::Level::Info:
    return {0.67f, 0.70f, 0.75f, 1.0f};
  default:
    return ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
  }
}

} // namespace

void ConsoleManager::draw() {
  const float step = ImGui::GetIO().DeltaTime / kSlideSeconds;
  slide_ = std::clamp(slide_ + (open_ ? step : -step), 0.0f, 1.0f);
  if (slide_ <= 0.0f) {
    return;
  }

  const ImGuiViewport *viewport = ImGui::GetMainViewport();
  const float height = std::floor(viewport->Size.y * kHeight);
  const float eased = 1.0f - (1.0f - slide_) * (1.0f - slide_);
  ImGui::SetNextWindowPos({viewport->Pos.x, viewport->Pos.y - std::floor(height * (1.0f - eased))});
  ImGui::SetNextWindowSize({viewport->Size.x, height});
  ImGui::SetNextWindowBgAlpha(0.94f);
  if (focus_input_) {
    ImGui::SetNextWindowFocus();
  }

  ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking |
      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
  if (!open_) {
    // Sliding away, so the game's input is its own again.
    flags |= ImGuiWindowFlags_NoInputs;
  }
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0f, 0.0f});
  const bool visible = ImGui::Begin("##console", nullptr, flags);
  // Only for this window, not for the menus and tooltips inside it.
  ImGui::PopStyleVar(3);
  if (visible) {
    // Above the game's windows, but not above its own context menu.
    if (!ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopup)) {
      ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
    }
    drawLog(height - inputHeight());
    drawInput();
  }
  ImGui::End();

  if (!open_ && ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow)) {
    ImGui::SetWindowFocus(nullptr);
  }
}

void ConsoleManager::drawLog(float height) {
  if (ImGui::BeginChild(
          "log", {0.0f, height}, ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_HorizontalScrollbar)) {
    const bool at_bottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0.0f, 1.0f});

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(lines_.size()));
    while (clipper.Step()) {
      for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
        const Line &line = lines_[static_cast<size_t>(i)];
        ImGui::PushStyleColor(ImGuiCol_Text, lineColor(line));
        ImGui::TextUnformatted(line.text.data(), line.text.data() + line.text.size());
        ImGui::PopStyleColor();
        if (!ImGui::IsItemHovered()) {
          continue;
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
          context_text_ = line.text;
        }
        if (!line.plain) {
          ImGui::SetTooltip("%s at %.3f s", line.location.c_str(), line.seconds);
        }
      }
    }
    ImGui::PopStyleVar();

    const float page = ImGui::GetWindowHeight() * 0.8f;
    if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) {
      ImGui::SetScrollY(ImGui::GetScrollY() - page);
    } else if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) {
      ImGui::SetScrollY(ImGui::GetScrollY() + page);
    } else if (at_bottom || scroll_to_bottom_) {
      ImGui::SetScrollHereY(1.0f);
    }
    scroll_to_bottom_ = false;

    if (ImGui::BeginPopupContextWindow("log_menu")) {
      if (ImGui::MenuItem("Copy line", nullptr, false, !context_text_.empty())) {
        ImGui::SetClipboardText(context_text_.c_str());
      }
      if (ImGui::MenuItem("Copy all")) {
        std::string all;
        for (const Line &line : lines_) {
          all += line.text;
          all += '\n';
        }
        ImGui::SetClipboardText(all.c_str());
      }
      if (ImGui::MenuItem("Clear")) {
        clear();
      }
      ImGui::EndPopup();
    } else {
      context_text_.clear();
    }
  }
  ImGui::EndChild();
}

void ConsoleManager::drawInput() {
  // Typing always goes to the input, even after a click on the log.
  const bool idle = !ImGui::IsAnyItemActive() && !ImGui::IsAnyMouseDown() &&
      ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
  if (open_ && (focus_input_ || idle)) {
    ImGui::SetKeyboardFocusHere();
    focus_input_ = false;
    restore_cursor_ = true;
  }

  // InputText reverts its text on Escape, which here closes things instead.
  const bool escape = open_ && ImGui::IsKeyPressed(ImGuiKey_Escape, false);
  const std::string text = text_.data();
  const size_t cursor = cursor_;
  const bool dropdown_visible = isDropdownVisible(completion());

  // Fills the bottom of the window edge to edge. ImGui's focus outline would
  // float outside it, so a line along the top shows focus instead.
  const float width = ImGui::GetWindowWidth();
  ImGui::SetCursorPos({0.0f, ImGui::GetWindowHeight() - inputHeight()});
  const ImVec2 corner = ImGui::GetCursorScreenPos();
  ImGui::PushStyleColor(ImGuiCol_NavCursor, 0);
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, kInputPadding);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
  ImGui::SetNextItemWidth(width);
  constexpr ImGuiInputTextFlags kFlags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackCompletion |
      ImGuiInputTextFlags_CallbackHistory | ImGuiInputTextFlags_CallbackAlways;
  const bool entered = ImGui::InputTextWithHint(
      "##input", "Type a command, or help", text_.data(), text_.size(), kFlags, &ConsoleManager::onInputText, this);
  ImGui::PopStyleVar(3);
  ImGui::PopStyleColor();
  ImGui::GetWindowDrawList()->AddLine(corner, {corner.x + width, corner.y}, ImGui::GetColorU32(accentColor()));

  if (entered) {
    submitInput();
  } else if (escape) {
    setText(text, cursor);
    closeDropdown();
    focus_input_ = true;
    if (!dropdown_visible) {
      setOpen(false);
    }
  }
  if (open_) {
    drawDropdown();
  }
}

void ConsoleManager::submitInput() {
  focus_input_ = true;
  const Completion current = completion();
  if (navigated_ && isDropdownVisible(current) && selected_ < current.candidates.size()) {
    accept(current, selected_);
    return;
  }

  std::string line = text_.data();
  line.erase(line.find_last_not_of(" \t") + 1);
  if (!line.empty()) {
    submit(line);
    if (history_.empty() || history_.back() != line) {
      history_.push_back(line);
    }
    if (history_.size() > kMaxHistory) {
      history_.erase(history_.begin());
    }
  }
  history_position_ = history_.size();
  setText("", 0);
  closeDropdown();
  scroll_to_bottom_ = true;
}

Completion ConsoleManager::completion() const {
  const std::string_view text = text_.data();
  return commands_.complete(text.substr(0, std::min(cursor_, text.size())));
}

bool ConsoleManager::isDropdownVisible(const Completion &completion) const {
  return dropdown_open_ && (completion.command != nullptr || !completion.candidates.empty());
}

void ConsoleManager::closeDropdown() {
  dropdown_open_ = false;
  navigated_ = false;
  selected_ = 0;
}

void ConsoleManager::setText(std::string_view text, size_t cursor) {
  const size_t length = std::min(text.size(), text_.size() - 1);
  text.copy(text_.data(), length);
  text_[length] = '\0';
  cursor_ = std::min(cursor, length);
  last_text_ = text_.data();
}

void ConsoleManager::accept(const Completion &completion, size_t index) {
  std::string text = text_.data();
  const std::string word = completion.candidates[index].text + ' ';
  text.replace(completion.start, cursor_ - completion.start, word);
  setText(text, completion.start + word.size());
  navigated_ = false;
  selected_ = 0;
  dropdown_open_ = true;
  focus_input_ = true;
}

// Below the word being typed, like in a code editor.
void ConsoleManager::drawDropdown() {
  const Completion current = completion();
  if (!isDropdownVisible(current)) {
    return;
  }
  selected_ = current.candidates.empty() ? 0 : std::min(selected_, current.candidates.size() - 1);

  const ImGuiStyle &style = ImGui::GetStyle();
  const size_t rows = std::min(current.candidates.size(), kMaxRows);
  const ImVec2 padding(8.0f, 6.0f);

  float width = 0.0f;
  float name_width = 0.0f;
  for (const Candidate &candidate : current.candidates) {
    name_width = std::max(name_width, textWidth(candidate.text));
  }
  for (const Candidate &candidate : current.candidates) {
    const float detail = candidate.detail.empty() ? 0.0f : kColumnGap + textWidth(candidate.detail);
    width = std::max(width, name_width + detail);
  }
  float height = static_cast<float>(rows) * rowHeight();
  if (current.command) {
    width = std::max({width, textWidth(current.command->usage()), textWidth(current.command->help)});
    height += ImGui::GetTextLineHeightWithSpacing() * (current.command->help.empty() ? 1.0f : 2.0f);
    // The separator, or no spacing after the last line.
    height += rows > 0 ? style.ItemSpacing.y + 1.0f : -style.ItemSpacing.y;
  }
  const float scrollbar = current.candidates.size() > rows ? style.ScrollbarSize + style.ItemSpacing.x : 0.0f;
  width = std::clamp(width + scrollbar, kMinDropdownWidth, kMaxDropdownWidth);
  const ImVec2 size(width + 2.0f * padding.x, height + 2.0f * padding.y);

  const ImGuiViewport *viewport = ImGui::GetMainViewport();
  const ImVec2 input_min = ImGui::GetItemRectMin();
  const ImVec2 input_max = ImGui::GetItemRectMax();
  const std::string_view before = std::string_view(text_.data()).substr(0, current.start);
  // The candidates line up with the text they complete.
  ImVec2 position(input_min.x + kInputPadding.x + textWidth(before) - padding.x, input_max.y + 4.0f);
  position.x = std::clamp(position.x, input_min.x, std::max(input_min.x, input_max.x - size.x));
  if (position.y + size.y > viewport->Pos.y + viewport->Size.y) {
    position.y = input_min.y - 4.0f - size.y;
  }

  ImGui::SetNextWindowPos(position);
  ImGui::SetNextWindowSize(size);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, padding);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, {1.0f, 1.0f});
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::GetStyleColorVec4(ImGuiCol_PopupBg));
  constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoNav |
      ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
  if (ImGui::Begin("##console_dropdown", nullptr, kFlags)) {
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
    if (current.command) {
      drawSignature(current);
      if (rows > 0) {
        ImGui::Separator();
      }
    }
    if (rows > 0) {
      drawCandidates(current, name_width);
    }
  }
  ImGui::End();
  ImGui::PopStyleColor();
  ImGui::PopStyleVar(2);
}

// The command's usage, with the parameter being typed in the accent color.
void ConsoleManager::drawSignature(const Completion &completion) {
  ImGui::TextUnformatted(completion.command->name.c_str());
  const std::vector<Parameter> &parameters = completion.command->parameters;
  for (size_t i = 0; i < parameters.size(); ++i) {
    ImGui::SameLine(0.0f, ImGui::CalcTextSize(" ").x);
    const bool is_current = i == completion.parameter;
    ImGui::PushStyleColor(ImGuiCol_Text, is_current ? accentColor() : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextUnformatted(parameters[i].usage().c_str());
    ImGui::PopStyleColor();
  }
  if (!completion.command->help.empty()) {
    ImGui::TextDisabled("%s", completion.command->help.c_str());
  }
}

void ConsoleManager::drawCandidates(const Completion &completion, float width) {
  const size_t typed = std::min(cursor_, std::string_view(text_.data()).size()) - completion.start;
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0.0f, 0.0f});
  // Neutral while Enter would run the line instead of accepting the row.
  ImVec4 highlight = navigated_ ? accentColor() : ImGui::GetStyleColorVec4(ImGuiCol_Text);
  highlight.w = navigated_ ? 0.25f : 0.08f;
  ImGui::PushStyleColor(ImGuiCol_Header, highlight);
  // Sized to the rows by the window around it.
  if (ImGui::BeginChild("candidates", {0.0f, 0.0f}, ImGuiChildFlags_None, ImGuiWindowFlags_NoNav)) {
    for (size_t i = 0; i < completion.candidates.size(); ++i) {
      const Candidate &candidate = completion.candidates[i];
      ImGui::PushID(static_cast<int>(i));
      if (ImGui::Selectable("##candidate", i == selected_, ImGuiSelectableFlags_None, {0.0f, rowHeight()})) {
        accept(completion, i);
      }
      if (scroll_to_selected_ && i == selected_) {
        ImGui::SetScrollHereY();
        scroll_to_selected_ = false;
      }

      ImDrawList *draw_list = ImGui::GetWindowDrawList();
      ImVec2 position = ImGui::GetItemRectMin();
      const float left = position.x;
      position.y += (rowHeight() - ImGui::GetTextLineHeight()) * 0.5f;
      const char *begin = candidate.text.data();
      const char *end = begin + candidate.text.size();
      const char *match_end = begin + std::min(typed, candidate.text.size());
      draw_list->AddText(position, ImGui::GetColorU32(accentColor()), begin, match_end);
      position.x += textWidth({begin, match_end});
      draw_list->AddText(position, ImGui::GetColorU32(ImGuiCol_Text), match_end, end);
      position.x = left + width + kColumnGap;
      draw_list->AddText(position, ImGui::GetColorU32(ImGuiCol_TextDisabled), candidate.detail.c_str());
      ImGui::PopID();
    }
  }
  ImGui::EndChild();
  ImGui::PopStyleColor();
  ImGui::PopStyleVar();
}

int ConsoleManager::onInputText(ImGuiInputTextCallbackData *data) {
  auto &console = *static_cast<ConsoleManager *>(data->UserData);
  if (console.restore_cursor_) {
    // Focusing from code selects everything.
    data->CursorPos = std::min(static_cast<int>(console.cursor_), data->BufTextLen);
    data->ClearSelection();
    console.restore_cursor_ = false;
  }

  const std::string_view text(data->Buf, static_cast<size_t>(data->BufTextLen));
  if (text != console.last_text_) {
    console.dropdown_open_ = !text.empty();
    console.navigated_ = false;
    console.selected_ = 0;
  }
  console.cursor_ = static_cast<size_t>(data->CursorPos);

  if (data->EventFlag == ImGuiInputTextFlags_CallbackCompletion) {
    console.completeInput(*data);
  } else if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory) {
    const Completion current = console.commands_.complete(text.substr(0, console.cursor_));
    if (console.isDropdownVisible(current) && !current.candidates.empty()) {
      console.moveSelection(data->EventKey == ImGuiKey_UpArrow ? -1 : 1, current.candidates.size());
    } else {
      console.browseHistory(*data);
    }
  }
  console.cursor_ = static_cast<size_t>(data->CursorPos);
  console.last_text_.assign(data->Buf, static_cast<size_t>(data->BufTextLen));
  return 0;
}

// Accepts the selected candidate, or opens the dropdown to choose one.
void ConsoleManager::completeInput(ImGuiInputTextCallbackData &data) {
  const Completion current = commands_.complete({data.Buf, static_cast<size_t>(data.CursorPos)});
  if (current.candidates.empty()) {
    return;
  }
  if (!dropdown_open_ && current.candidates.size() > 1) {
    dropdown_open_ = true;
    return;
  }
  const std::string word = current.candidates[std::min(selected_, current.candidates.size() - 1)].text + ' ';
  const auto start = static_cast<int>(current.start);
  data.DeleteChars(start, data.CursorPos - start);
  data.InsertChars(data.CursorPos, word.c_str());
  dropdown_open_ = true;
  navigated_ = false;
  selected_ = 0;
}

void ConsoleManager::moveSelection(int step, size_t count) {
  selected_ = (selected_ + count + static_cast<size_t>(step)) % count;
  navigated_ = true;
  scroll_to_selected_ = true;
}

void ConsoleManager::browseHistory(ImGuiInputTextCallbackData &data) {
  if (data.EventKey == ImGuiKey_UpArrow) {
    if (history_position_ == 0) {
      return;
    }
    if (history_position_ == history_.size()) {
      draft_.assign(data.Buf, static_cast<size_t>(data.BufTextLen));
    }
    --history_position_;
  } else {
    if (history_position_ == history_.size()) {
      return;
    }
    ++history_position_;
  }
  const std::string &text = history_position_ < history_.size() ? history_[history_position_] : draft_;
  data.DeleteChars(0, data.BufTextLen);
  data.InsertChars(0, text.c_str());
  closeDropdown();
}

} // namespace strafe::console
