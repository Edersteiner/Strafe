#include "core/application.h"

#include "SDL3/SDL_events.h"
#include "demo/week1_house.h"
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
    if (!demo::createHouseMesh(renderer_, house_)) {
      log::error("Failed to create the house mesh");
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
  while (running_) {
    SDL_Event event{0};
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_EVENT_QUIT) {
        running_ = false;
        break;
      } else if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
        SDL_GetWindowSizeInPixels(window_, &width_, &height_);
        swapchain_.markForRecreate();
      } else if (event.type == SDL_EVENT_KEY_DOWN) {
        if (event.key.key == SDLK_SPACE && !event.key.repeat) {
          demo::cycleRoofColor(house_);
        }
      }
    }
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

  demo::buildHouse(frame_desc_, house_);

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
