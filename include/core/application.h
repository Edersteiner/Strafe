#pragma once

#include "gfx/device.h"
#include "gfx/framering.h"
#include "gfx/swapchain.h"
#include "renderer/frame_desc.h"
#include "renderer/renderer.h"

// Week 2 Demo
#include "demo/week2_scene.h"

#include <SDL3/SDL.h>

namespace strafe {

class Application {
public:
  bool initialize();
  void run();
  void shutdown();

private:
  bool initializeVulkan();

  void render();

  bool running_ = false;

  SDL_Window *window_ = nullptr;
  int width_ = 1280, height_ = 720;

  gfx::Device device_;
  gfx::Swapchain swapchain_;
  gfx::FrameRing frame_ring_;

  renderer::Renderer renderer_;
  renderer::FrameDesc frame_desc_;

  // Week 2 Demo
  demo::Scene scene_;
};
} // namespace strafe
