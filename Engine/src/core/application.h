#pragma once

#include "input/input_manager.h"
#include "strafe/app.h"
#include "strafe/core/type_registry.h"
#include "strafe/engine.h"
#include "strafe/gfx/device.h"
#include "strafe/gfx/framering.h"
#include "strafe/gfx/swapchain.h"
#include "strafe/renderer/renderer.h"

#include <SDL3/SDL.h>

#include <cstdint>

namespace strafe {

// The engine's main loop and everything it owns: the window, the GPU and the
// renderer. Private to the engine; apps see it through run() and the
// Engine, Input and Renderer facades.
class Application {
public:
  explicit Application(App &app) : app_(app) {}

  Application(const Application &) = delete;
  Application &operator=(const Application &) = delete;

  bool initialize(const AppConfig &config);
  void run();
  void shutdown();

  SDL_Window &window() { return *window_; }
  gfx::Device &device() { return device_; }
  gfx::Swapchain &swapchain() { return swapchain_; }

  renderer::Renderer &renderer() { return renderer_; }

  // Behind Engine.
  const reflect::TypeRegistry &types() const { return types_; }
  float time() const;
  void quit() { running_ = false; }

private:
  bool registerTypes();
  // Points the facades at this application's subsystems, or at nothing.
  void bindFacades();
  void unbindFacades();
  bool initializeVulkan();
  void render();

  App &app_;
  // onShutdown only runs for an app whose onInit was called.
  bool app_initialized_ = false;
  bool running_ = false;
  uint64_t start_ticks_ns_ = 0;

  reflect::TypeRegistry types_;

  SDL_Window *window_ = nullptr;
  int width_ = 0, height_ = 0;

  gfx::Device device_;
  gfx::Swapchain swapchain_;
  gfx::FrameRing frame_ring_;

  input::InputManager input_;
  renderer::Renderer renderer_;
};

} // namespace strafe
