#include "application.h"

#include "strafe/console.h"
#include "strafe/core/log.h"
#include "strafe/gfx/vk_check.h"
#include "strafe/input.h"
#include "strafe/renderer.h"
#include "strafe/ui/imgui_impl.h"
#include "strafe/world.h"

#include <cassert>
#include <optional>

#include <imgui.h>

namespace strafe {

bool Application::initialize(const AppConfig &config) {
  start_ticks_ns_ = SDL_GetTicksNS();
  console_.initialize(input_);

  if (!registerGenerated()) {
    return false;
  }

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

  // Gamepads are optional, so the game still runs on keyboard and mouse.
  if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
    log::warn("Gamepads unavailable: {}", SDL_GetError());
  }
  input_.initialize(window_);

  if (!initializeVulkan()) {
    log::error("Failed to initialize Vulkan");
    return false;
  }

  ui::initializeImgui(*this);

  bindFacades();
  // Before onInit, so the game can change the engine's actions there.
  Input::bind(input::kToggleConsole, {input::Key::Grave});
  app_initialized_ = true;
  if (!app_.onInit()) {
    log::error("The app failed to initialize");
    return false;
  }
  return true;
}

bool Application::registerGenerated() {
  types_.addBuiltins();
  // The engine's first, so a game type that clashes with one is the one reported.
  generated::registerEngineTypes(types_);
  generated::registerAppTypes(types_);
  if (types_.failed()) {
    log::error("Failed to register reflected types");
    return false;
  }
  log::debug("Registered {} types", types_.types().size());

  console_.commands().setApp(app_);
  generated::registerEngineCommands(console_.commands());
  generated::registerAppCommands(console_.commands());

  generated::registerEngineComponents(components_);
  generated::registerAppComponents(components_);

  generated::registerEngineSystems(systems_);
  generated::registerAppSystems(systems_);
  if (!systems_.finalize()) {
    log::error("Failed to order the systems");
    return false;
  }
  return true;
}

void Application::bindFacades() {
  Console::manager_ = &console_;
  Engine::application_ = this;
  Input::manager_ = &input_;
  Renderer::renderer_ = &renderer_;
  World::entities_ = &entities_;
}

void Application::unbindFacades() {
  Console::manager_ = nullptr;
  Engine::application_ = nullptr;
  Input::manager_ = nullptr;
  Renderer::renderer_ = nullptr;
  World::entities_ = nullptr;
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
    input_.beginFrame();
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
      dispatchEvent(event);
    }
    if (!running_) {
      break;
    }
    input_.update();
    if (input_.isEngineActionJustPressed(input::kToggleConsole)) {
      console_.setOpen(!console_.isOpen());
    }
    renderer_.beginFrame(time());
    console_.update();

    const uint64_t now_ns = SDL_GetTicksNS();
    delta_seconds_ = static_cast<float>(now_ns - last_ticks_ns) / 1'000'000'000.0f;
    last_ticks_ns = now_ns;
    app_.onUpdate(delta_seconds_);
    systems_.run(ecs::Stage::Update, entities_, delta_seconds_);

    render();
  }
}

void Application::dispatchEvent(const SDL_Event &event) {
  // The console's own input field must not swallow the key that closes it. In
  // any other field, that key types as usual.
  if ((console_.isOpen() || !ui::wantsText()) && input_.pressesEngineAction(event)) {
    drop_text_ = true;
    input_.handleEvents(event);
    return;
  }
  if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) {
    drop_text_ = false;
  } else if (event.type == SDL_EVENT_TEXT_INPUT && drop_text_) {
    return;
  }
  if (!ui::processImguiEvent(event)) {
    input_.handleEvents(event);
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
  }

  // Minimized, or the swapchain is out of date until the next resize.
  std::optional<gfx::FrameContext> frame = frame_ring_.beginFrame(swapchain_);
  if (!frame) {
    return;
  }

  ui::beginRenderImgui(input_.isMouseCaptured());

  systems_.run(ecs::Stage::Render, entities_, delta_seconds_);
  app_.onRender();
  console_.draw();

  ui::endRenderImgui();

  renderer_.render(*frame);
  frame_ring_.endFrame(*frame, swapchain_);
}

void Application::shutdown() {
  if (device_.logicalDevice()) {
    VK_CHECK(vkDeviceWaitIdle(device_.logicalDevice()));
  }

  // Before the renderer, so the app can still use its ids.
  if (app_initialized_) {
    app_.onShutdown();
  }
  unbindFacades();
  // Components can hold renderer resources, so they go before the renderer.
  entities_.clear();

  ui::shutdownImgui();

  // The renderer defers destruction into the ring, so it shuts down first.
  renderer_.shutdown();
  frame_ring_.shutdown();
  swapchain_.shutdown();
  device_.shutdown();

  input_.shutdown();
  if (window_) {
    SDL_DestroyWindow(window_);
    window_ = nullptr;
  }
  SDL_Quit();
  console_.shutdown();
}

float Application::time() const { return static_cast<float>(SDL_GetTicksNS() - start_ticks_ns_) / 1'000'000'000.0f; }

Application *Engine::application_ = nullptr;

Application &Engine::application() {
  assert(application_ && "Engine is only usable from App::onInit until App::onShutdown returns");
  return *application_;
}

const reflect::TypeRegistry &Engine::types() { return application().types(); }
const ecs::ComponentRegistry &Engine::components() { return application().components(); }
ecs::Schedule &Engine::systems() { return application().systems(); }
float Engine::time() { return application().time(); }
void Engine::quit() { application().quit(); }

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
