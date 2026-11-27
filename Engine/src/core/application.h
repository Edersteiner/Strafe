#pragma once

#include "console/console_manager.h"
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
// Console, Engine, Input and Renderer facades.
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
  bool registerGenerated();
  // Points the facades at this application's subsystems, or at nothing.
  void bindFacades();
  void unbindFacades();
  bool initializeVulkan();
  // Gives the event to the console, the UI or the game.
  void dispatchEvent(const SDL_Event &event);
  void render();

  App &app_;
  // onShutdown only runs for an app whose onInit was called.
  bool app_initialized_ = false;
  bool running_ = false;
  uint64_t start_ticks_ns_ = 0;

  reflect::TypeRegistry types_;
  console::ConsoleManager console_;
  // Text from a key that triggered an engine action is dropped so the key
  // that closes the console doesn't type into it.
  bool drop_text_ = false;

  SDL_Window *window_ = nullptr;
  int width_ = 0, height_ = 0;

  gfx::Device device_;
  gfx::Swapchain swapchain_;
  gfx::FrameRing frame_ring_;

  input::InputManager input_;
  renderer::Renderer renderer_;
};

} // namespace strafe
