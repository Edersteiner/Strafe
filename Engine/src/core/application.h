#pragma once

#include "strafe/app.h"
#include "strafe/engine.h"
#include "strafe/gfx/device.h"
#include "strafe/gfx/framering.h"
#include "strafe/gfx/swapchain.h"
#include "strafe/renderer/frame_desc.h"
#include "strafe/renderer/renderer.h"

#include <SDL3/SDL.h>

#include <cstdint>

namespace strafe {

// The engine's main loop and everything it owns: the window, the GPU and the
// renderer. Private to the engine; apps see it through Engine and run().
class Application {
public:
  explicit Application(App &app) : app_(app), engine_(*this) {}

  Application(const Application &) = delete;
  Application &operator=(const Application &) = delete;

  bool initialize(const AppConfig &config);
  void run();
  void shutdown();

  // Behind Engine.
  renderer::Renderer &renderer() { return renderer_; }
  float aspect() const;
  float time() const;
  void quit() { running_ = false; }

private:
  bool initializeVulkan();
  void render();

  App &app_;
  Engine engine_;
  // onShutdown only runs for an app whose onInit was called.
  bool app_initialized_ = false;
  bool running_ = false;
  uint64_t start_ticks_ns_ = 0;

  SDL_Window *window_ = nullptr;
  int width_ = 0, height_ = 0;

  gfx::Device device_;
  gfx::Swapchain swapchain_;
  gfx::FrameRing frame_ring_;

  renderer::Renderer renderer_;
  renderer::FrameDesc frame_desc_;
};

} // namespace strafe
