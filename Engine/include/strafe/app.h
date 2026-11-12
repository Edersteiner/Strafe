#pragma once

#include <SDL3/SDL_events.h>

#include <string>

namespace strafe {

class Engine;

namespace renderer {
struct FrameDesc;
} // namespace renderer

struct AppConfig {
  std::string title = "Strafe";
  // The window's starting size in screen coordinates.
  int width = 1280;
  int height = 720;
};

// Each game implements this
class App {
public:
  virtual ~App() = default;

  // Load meshes, textures, shaders and materials. Returning false quits
  // before the first frame.
  virtual bool onInit(Engine &engine) = 0;

  // Every SDL event except quit and resize, which the engine handles.
  // Todo: switch to own input system, not raw SDL Events.
  virtual void onEvent(Engine &engine, const SDL_Event &event) {}

  // Delta seconds since the previous frame.
  virtual void onUpdate(Engine &engine, float delta_seconds) {}

  // Fill in the camera, light and draws. The frame starts out default
  // constructed with no draws.
  virtual void onRender(Engine &engine, renderer::FrameDesc &frame) = 0;

  // Runs while the GPU and renderer are still alive, so ids are still valid.
  virtual void onShutdown(Engine &engine) {}
};

// Opens the window, runs the app until it or the user quits, and tears
// everything down. Returns the process exit code.
int run(App &app, const AppConfig &config = {});

} // namespace strafe
