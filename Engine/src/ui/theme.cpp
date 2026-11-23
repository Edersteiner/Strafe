#include "strafe/ui/theme.h"

#include <imgui.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <system_error>

namespace strafe::ui {

namespace {

constexpr ImVec4 hex(uint32_t rgb, float alpha = 1.0f) {
  return {
      static_cast<float>((rgb >> 16) & 0xFF) / 255.0f, static_cast<float>((rgb >> 8) & 0xFF) / 255.0f,
      static_cast<float>(rgb & 0xFF) / 255.0f, alpha};
}

constexpr uint32_t kDeep = 0x0B0B0D;    // Title bars, menu bar, docking space, tab strip.
constexpr uint32_t kSurface = 0x111214; // Windows.
constexpr uint32_t kRaised = 0x17181B;  // Popups, headers, table headers.
constexpr uint32_t kControl = 0x1B1C20; // Inputs, buttons.
constexpr uint32_t kHover = 0x232429;
constexpr uint32_t kPressed = 0x2B2D33;

constexpr uint32_t kBorder = 0x222327;
constexpr uint32_t kBorderStrong = 0x2E3036;
constexpr uint32_t kText = 0xD8D9DC;
constexpr uint32_t kTextMuted = 0x6B6E76;

constexpr uint32_t kAccent = 0x5E8BFF;
constexpr uint32_t kAccentBright = 0x7EA2FF;

void applySizes(ImGuiStyle &style) {
  style.WindowPadding = {8.0f, 8.0f};
  style.FramePadding = {7.0f, 4.0f};
  style.CellPadding = {4.0f, 2.0f};
  style.ItemSpacing = {7.0f, 5.0f};
  style.ItemInnerSpacing = {4.0f, 4.0f};
  style.IndentSpacing = 12.0f;
  style.ScrollbarSize = 10.0f;
  style.GrabMinSize = 8.0f;

  style.WindowRounding = 6.0f;
  style.ChildRounding = 4.0f;
  style.PopupRounding = 6.0f;
  style.FrameRounding = 4.0f;
  style.ScrollbarRounding = 8.0f;
  style.GrabRounding = 3.0f;
  style.TabRounding = 4.0f;

  style.WindowBorderSize = 1.0f;
  style.ChildBorderSize = 1.0f;
  style.PopupBorderSize = 1.0f;
  style.FrameBorderSize = 1.0f;
  style.TabBorderSize = 0.0f;
  style.TabBarBorderSize = 1.0f;
  style.TabBarOverlineSize = 1.0f;
  style.DockingSeparatorSize = 1.0f;
  style.SeparatorTextBorderSize = 1.0f;
  style.SeparatorTextPadding = {8.0f, 2.0f};

  style.WindowTitleAlign = {0.0f, 0.5f};
  style.WindowMenuButtonPosition = ImGuiDir_None;
  style.ColorButtonPosition = ImGuiDir_Right;
}

void applyColors(ImGuiStyle &style) {
  ImVec4 *c = style.Colors;

  c[ImGuiCol_Text] = hex(kText);
  c[ImGuiCol_TextDisabled] = hex(kTextMuted);
  c[ImGuiCol_TextLink] = hex(kAccentBright);
  c[ImGuiCol_TextSelectedBg] = hex(kAccent, 0.30f);
  c[ImGuiCol_InputTextCursor] = hex(kText);

  c[ImGuiCol_WindowBg] = hex(kSurface);
  c[ImGuiCol_ChildBg] = hex(kSurface, 0.0f);
  c[ImGuiCol_PopupBg] = hex(kRaised);
  c[ImGuiCol_MenuBarBg] = hex(kDeep);
  c[ImGuiCol_ModalWindowDimBg] = hex(0x000000, 0.60f);

  c[ImGuiCol_Border] = hex(kBorder);
  c[ImGuiCol_BorderShadow] = hex(0x000000, 0.0f);

  c[ImGuiCol_FrameBg] = hex(kControl);
  c[ImGuiCol_FrameBgHovered] = hex(kHover);
  c[ImGuiCol_FrameBgActive] = hex(kPressed);

  c[ImGuiCol_TitleBg] = hex(kDeep);
  c[ImGuiCol_TitleBgActive] = hex(kDeep);
  c[ImGuiCol_TitleBgCollapsed] = hex(kDeep);

  c[ImGuiCol_ScrollbarBg] = hex(0x000000, 0.0f);
  c[ImGuiCol_ScrollbarGrab] = hex(kPressed);
  c[ImGuiCol_ScrollbarGrabHovered] = hex(kBorderStrong);
  c[ImGuiCol_ScrollbarGrabActive] = hex(kTextMuted);

  c[ImGuiCol_CheckMark] = hex(kAccent);
  c[ImGuiCol_CheckboxSelectedBg] = hex(kControl);
  c[ImGuiCol_SliderGrab] = hex(kAccent);
  c[ImGuiCol_SliderGrabActive] = hex(kAccentBright);

  c[ImGuiCol_Button] = hex(kControl);
  c[ImGuiCol_ButtonHovered] = hex(kHover);
  c[ImGuiCol_ButtonActive] = hex(kPressed);

  c[ImGuiCol_Header] = hex(kRaised);
  c[ImGuiCol_HeaderHovered] = hex(kHover);
  c[ImGuiCol_HeaderActive] = hex(kPressed);

  c[ImGuiCol_Separator] = hex(kBorder);
  c[ImGuiCol_SeparatorHovered] = hex(kAccent, 0.60f);
  c[ImGuiCol_SeparatorActive] = hex(kAccent);

  c[ImGuiCol_ResizeGrip] = hex(0x000000, 0.0f);
  c[ImGuiCol_ResizeGripHovered] = hex(kAccent, 0.50f);
  c[ImGuiCol_ResizeGripActive] = hex(kAccent);

  c[ImGuiCol_Tab] = hex(kDeep);
  c[ImGuiCol_TabHovered] = hex(kHover);
  c[ImGuiCol_TabSelected] = hex(kSurface);
  c[ImGuiCol_TabSelectedOverline] = hex(kAccent);
  c[ImGuiCol_TabDimmed] = hex(kDeep);
  c[ImGuiCol_TabDimmedSelected] = hex(kSurface);
  c[ImGuiCol_TabDimmedSelectedOverline] = hex(0x000000, 0.0f);

  c[ImGuiCol_DockingPreview] = hex(kAccent, 0.35f);
  c[ImGuiCol_DockingEmptyBg] = hex(kDeep);

  c[ImGuiCol_PlotLines] = hex(kAccent);
  c[ImGuiCol_PlotLinesHovered] = hex(kAccentBright);
  c[ImGuiCol_PlotHistogram] = hex(kAccent);
  c[ImGuiCol_PlotHistogramHovered] = hex(kAccentBright);

  c[ImGuiCol_TableHeaderBg] = hex(kRaised);
  c[ImGuiCol_TableBorderStrong] = hex(kBorder);
  c[ImGuiCol_TableBorderLight] = hex(kBorder, 0.60f);
  c[ImGuiCol_TableRowBg] = hex(0x000000, 0.0f);
  c[ImGuiCol_TableRowBgAlt] = hex(0xFFFFFF, 0.02f);
  c[ImGuiCol_TreeLines] = hex(kBorderStrong);

  c[ImGuiCol_DragDropTarget] = hex(kAccent);
  c[ImGuiCol_DragDropTargetBg] = hex(kAccent, 0.12f);
  c[ImGuiCol_UnsavedMarker] = hex(kText);

  c[ImGuiCol_NavCursor] = hex(kAccent);
  c[ImGuiCol_NavWindowingHighlight] = hex(0xFFFFFF, 0.60f);
  c[ImGuiCol_NavWindowingDimBg] = hex(0x000000, 0.50f);
}

void loadFont() {
  constexpr std::array kCandidates{
      "C:/Windows/Fonts/segoeui.ttf",
      "/usr/share/fonts/inter/Inter-Regular.ttf",
      "/usr/share/fonts/TTF/Inter-Regular.ttf",
      "/usr/share/fonts/truetype/inter/Inter-Regular.ttf",
      "/usr/share/fonts/noto/NotoSans-Regular.ttf",
      "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
      "/usr/share/fonts/TTF/DejaVuSans.ttf",
      "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
      "/System/Library/Fonts/SFNS.ttf",
  };
  ImFontAtlas *fonts = ImGui::GetIO().Fonts;
  for (const char *candidate : kCandidates) {
    std::error_code error;
    if (std::filesystem::exists(candidate, error) && fonts->AddFontFromFileTTF(candidate)) {
      return;
    }
  }
  fonts->AddFontDefaultVector();
}

} // namespace

void applyTheme(float dpi_scale) {
  ImGuiStyle &style = ImGui::GetStyle();
  // Start from the stock dark style, so any color added in a later ImGui
  // version still gets a sensible value.
  ImGui::StyleColorsDark(&style);
  applySizes(style);
  applyColors(style);

  style.ScaleAllSizes(dpi_scale);
  style.FontScaleDpi = dpi_scale;

  loadFont();
  style.FontSizeBase = 15.0f;
}

} // namespace strafe::ui
