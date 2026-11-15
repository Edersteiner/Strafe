#include "application.h"

#include "strafe/core/log.h"
#include "strafe/gfx/vk_check.h"

#include <optional>
#include <utility>
#include <vector>

namespace strafe {

bool Application::initialize(const AppConfig &config) {
  start_ticks_ns_ = SDL_GetTicksNS();

  if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
    log::error("Failed to initialise SDL Subsystems: {}", SDL_GetError());
    return false;
  }

  window_ =
      SDL_CreateWindow(config.title.c_str(), config.width, config.height, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
  if (!window_) {
    log::error("Failed to create window: {}", SDL_GetError());
    return false;
  }
  SDL_GetWindowSizeInPixels(window_, &width_, &height_);

  if (!initializeVulkan()) {
    log::error("Failed to initialize Vulkan");
    return false;
  }

  app_initialized_ = true;
  if (!app_.onInit(engine_)) {
    log::error("The app failed to initialize");
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
          device_, VkExtent2D{.width = static_cast<uint32_t>(width_), .height = static_cast<uint32_t>(height_)})) {
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
      }
      if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
        SDL_GetWindowSizeInPixels(window_, &width_, &height_);
        swapchain_.markForRecreate();
        continue;
      }
      app_.onEvent(engine_, event);
    }
    if (!running_) {
      break;
    }

    const uint64_t now_ns = SDL_GetTicksNS();
    const float delta_seconds = static_cast<float>(now_ns - last_ticks_ns) / 1'000'000'000.0f;
    last_ticks_ns = now_ns;
    app_.onUpdate(engine_, delta_seconds);

    render();
  }
}

void Application::render() {
  if (swapchain_.requireRecreate()) {
    if (!swapchain_.recreate(
            VkExtent2D{.width = static_cast<uint32_t>(width_), .height = static_cast<uint32_t>(height_)})) {
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

  // Minimized, or the swapchain is out of date until the next resize.
  std::optional<gfx::FrameContext> frame = frame_ring_.beginFrame(swapchain_);
  if (!frame) {
    return;
  }

  // A fresh frame each time, but the draw list keeps its allocation.
  std::vector<renderer::DrawItem> draws = std::move(frame_desc_.draws);
  draws.clear();
  frame_desc_ = {};
  frame_desc_.draws = std::move(draws);
  frame_desc_.time = time();

  app_.onRender(engine_, frame_desc_);

  renderer_.render(*frame, frame_desc_);
  frame_ring_.endFrame(*frame, swapchain_);
}

void Application::shutdown() {
  if (device_.logicalDevice()) {
    VK_CHECK(vkDeviceWaitIdle(device_.logicalDevice()));
  }

  // Before the renderer, so the app can still use its ids.
  if (app_initialized_) {
    app_.onShutdown(engine_);
  }

  // The renderer defers destruction into the ring, so it shuts down first.
  renderer_.shutdown();
  frame_ring_.shutdown();
  swapchain_.shutdown();
  device_.shutdown();

  if (window_) {
    SDL_DestroyWindow(window_);
    window_ = nullptr;
  }
  SDL_Quit();
}

float Application::aspect() const {
  const VkExtent2D extent = swapchain_.extent();
  if (extent.width == 0 || extent.height == 0) {
    return 1.0f;
  }
  return static_cast<float>(extent.width) / static_cast<float>(extent.height);
}

float Application::time() const { return static_cast<float>(SDL_GetTicksNS() - start_ticks_ns_) / 1'000'000'000.0f; }

// Engine forwards to the Application that owns it.

renderer::Renderer &Engine::renderer() { return application_->renderer(); }
float Engine::aspect() const { return application_->aspect(); }
float Engine::time() const { return application_->time(); }
void Engine::quit() { application_->quit(); }

int run(App &app, const AppConfig &config) {
  Application application(app);
  const bool initialized = application.initialize(config);
  if (initialized) {
    application.run();
  }
  application.shutdown();
  return initialized ? 0 : 1;
}

} // namespace strafe
