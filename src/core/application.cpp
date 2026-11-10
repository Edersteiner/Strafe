#include "core/application.h"

#include "SDL3/SDL_events.h"
#include "demo/week3_scene.h"
#include <cstdint>
#include <optional>

#include "core/log.h"
#include "gfx/vk_check.h"

namespace strafe {

bool Application::initialize() {
  if (SDL_InitSubSystem(SDL_INIT_VIDEO)) {
    window_ = SDL_CreateWindow("Strafe", width_, height_,
                               SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);

    if (!window_) {
      log::error("Failed to create window: {}", SDL_GetError());
      return false;
    }

    SDL_GetWindowSizeInPixels(window_, &width_, &height_);

    if (!initializeVulkan()) {
      log::error("Failed to initialize Vulkan");
      return false;
    }
    if (!demo::createSceneMeshes(renderer_, scene_)) {
      log::error("Failed to create the scene meshes");
      return false;
    }
  } else {
    log::error("Failed to initialise SDL Subsystems: {}", SDL_GetError());
    return false;
  }

  return true;
}

bool Application::initializeVulkan() {
  if (!device_.initialize(window_, swapchain_.format())) {
    log::error("Failed to initialize the GPU device");
    return false;
  }
  if (!swapchain_.initialize(
          device_, VkExtent2D{.width = static_cast<uint32_t>(width_),
                              .height = static_cast<uint32_t>(height_)})) {
    log::error("Failed to create swapchain");
    return false;
  }
  if (!frame_ring_.initialize(device_)) {
    log::error("Failed to initialize frame ring");
    return false;
  }
  if (!renderer_.initialize(device_, swapchain_, frame_ring_)) {
    log::error("Failed to initialize renderer");
    return false;
  }
  return true;
}

void Application::run() {
  running_ = true;
  uint64_t last_ticks_ns = SDL_GetTicksNS();
  while (running_) {
    SDL_Event event{0};
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_EVENT_QUIT) {
        running_ = false;
        break;
      } else if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
        SDL_GetWindowSizeInPixels(window_, &width_, &height_);
        swapchain_.markForRecreate();
      } else if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat &&
                 event.key.key == SDLK_P) {
        demo::toggleProjection(scene_);
      }
    }

    const uint64_t now_ns = SDL_GetTicksNS();
    const float delta_seconds =
        static_cast<float>(now_ns - last_ticks_ns) / 1'000'000'000.0f;
    last_ticks_ns = now_ns;
    demo::updateScene(scene_, delta_seconds);

    render();
  }
}

void Application::render() {
  if (swapchain_.requireRecreate()) {
    if (!swapchain_.recreate(
            VkExtent2D{.width = static_cast<uint32_t>(width_),
                       .height = static_cast<uint32_t>(height_)})) {
      log::error("Cannot recreate swapchain!");
      running_ = false;
      return;
    }

    if (swapchain_.handle() && !renderer_.resize(swapchain_.extent())) {
      log::error("Cannot resize renderer!");
      running_ = false;
      return;
    }
  }

  std::optional<gfx::FrameContext> frame = frame_ring_.beginFrame(swapchain_);
  if (!frame) {
    return;
  }

  frame_desc_.draws.clear();

  const VkExtent2D extent = swapchain_.extent();
  const float aspect =
      static_cast<float>(extent.width) / static_cast<float>(extent.height);
  demo::buildScene(frame_desc_, scene_, aspect);

  renderer_.render(*frame, frame_desc_);
  frame_ring_.endFrame(*frame, swapchain_);
}

void Application::shutdown() {
  if (device_.logicalDevice()) {
    VK_CHECK(vkDeviceWaitIdle(device_.logicalDevice()));
  }

  // The renderer defers destruction into the ring, so it shuts down first.
  renderer_.shutdown();
  frame_ring_.shutdown();
  swapchain_.shutdown();
  device_.shutdown();

  if (window_) {
    SDL_DestroyWindow(window_);
  }
  SDL_Quit();
}

} // namespace strafe
