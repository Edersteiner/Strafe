#pragma once

namespace strafe {
class Application;
}

union SDL_Event;

namespace strafe::ui {

void initializeImgui(Application &app);
// Feeds the event to ImGui. Returns true if ImGui wants it, so the game
// shouldn't see it. Releases are never consumed, so no key or button sticks.
bool processImguiEvent(const SDL_Event &event);
// While input is captured (relative mouse mode), ImGui ignores the mouse and
// keyboard, so nothing stays hovered and keys don't type into widgets.
void beginRenderImgui(bool input_captured);
void endRenderImgui();
void shutdownImgui();

} // namespace strafe::ui
