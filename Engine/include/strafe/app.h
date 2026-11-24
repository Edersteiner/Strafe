#pragma once

#include <string>

namespace strafe {

struct AppConfig {
  std::string title = "Strafe";
  // The window's starting size in screen coordinates.
  int width = 1280;
  int height = 720;
};

// Each game implements this. Engine, Input and Renderer are usable from
// onInit until onShutdown returns.
class App {
public:
  virtual ~App() = default;

  // Load meshes, textures, shaders and materials. Returning false quits
  // before the first frame.
  virtual bool onInit() = 0;

  // Delta seconds since the previous frame. Draws can be added here or in
  // onRender.
  virtual void onUpdate(float delta_seconds) {}

  // Fill in Renderer::frame's camera and light, and add draws. Runs once the
  // GPU has a frame to draw into, inside the ImGui frame.
  virtual void onRender() = 0;

  // Runs while the GPU and renderer are still alive, so ids are still valid.
  virtual void onShutdown() {}
};

// Opens the window, runs the app until it or the user quits, and tears
// everything down. Returns the process exit code.
int run(App &app, const AppConfig &config = {});

} // namespace strafe
