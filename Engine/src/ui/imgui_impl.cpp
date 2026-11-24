#include "strafe/ui/imgui_impl.h"

#include "core/application.h"
#include "strafe/renderer/renderer.h"

#include "strafe/gfx/command_list.h"
#include "strafe/gfx/vk_check.h"
#include "strafe/ui/theme.h"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

namespace strafe::ui {

namespace {
static void checkVkResult(VkResult err) { VK_CHECK(err); }
} // namespace

void initializeImgui(Application &app) {
  const renderer::Renderer &renderer = app.renderer();

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

  float dpi_scale = SDL_GetWindowDisplayScale(&app.window());
  if (dpi_scale <= 0.0f) {
    dpi_scale = 1.0f;
  }
  applyTheme(dpi_scale);

  const VkFormat color_format = app.swapchain().format();

  ImGui_ImplSDL3_InitForVulkan(&app.window());
  ImGui_ImplVulkan_InitInfo init_info = {
      .ApiVersion = app.device().apiVersion(),
      .Instance = app.device().instance(),
      .PhysicalDevice = app.device().physicalDevice(),
      .Device = app.device().logicalDevice(),
      .QueueFamily = app.device().graphicsQueueFamily(),
      .Queue = app.device().graphicsQueue(),
      .DescriptorPool = nullptr,
      .DescriptorPoolSize = IMGUI_IMPL_VULKAN_MINIMUM_SAMPLER_POOL_SIZE,
      .MinImageCount = 2,
      .ImageCount = app.swapchain().imageCount(),
      .PipelineCache = nullptr, // No pipeline cache yet
      .PipelineInfoMain =
          {.MSAASamples = VK_SAMPLE_COUNT_1_BIT,
           .PipelineRenderingCreateInfo =
               {
                   .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
                   .colorAttachmentCount = 1,
                   .pColorAttachmentFormats = &color_format,
               }},
      .UseDynamicRendering = true,
      .Allocator = nullptr,
      .CheckVkResultFn = checkVkResult};
  ImGui_ImplVulkan_Init(&init_info);

  app.renderer().setOverlay([](gfx::CommandList &cmd) {
    if (ImDrawData *draw_data = ImGui::GetDrawData()) {
      ImGui_ImplVulkan_RenderDrawData(draw_data, cmd.handle());
    }
  });
} // namespace strafe::ui

bool processImguiEvent(const SDL_Event &event) {
  ImGui_ImplSDL3_ProcessEvent(&event);

  // Both flags describe the previous frame, which is how ImGui intends them to be used.
  const ImGuiIO &io = ImGui::GetIO();
  switch (event.type) {
  case SDL_EVENT_MOUSE_MOTION:
  case SDL_EVENT_MOUSE_WHEEL:
  case SDL_EVENT_MOUSE_BUTTON_DOWN:
    return io.WantCaptureMouse;
  case SDL_EVENT_KEY_DOWN:
  case SDL_EVENT_TEXT_INPUT:
    return io.WantCaptureKeyboard;
  default:
    return false;
  }
}

void beginRenderImgui(bool input_captured) {
  // NoMouseCursorChange stops the SDL3 backend from showing the cursor that
  // relative mouse mode hides.
  constexpr ImGuiConfigFlags kCapturedFlags =
      ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoKeyboard | ImGuiConfigFlags_NoMouseCursorChange;
  ImGuiIO &io = ImGui::GetIO();
  if (input_captured) {
    io.ConfigFlags |= kCapturedFlags;
  } else {
    io.ConfigFlags &= ~kCapturedFlags;
  }

  ImGui_ImplVulkan_NewFrame();
  ImGui_ImplSDL3_NewFrame();
  ImGui::NewFrame();
}

void endRenderImgui() { ImGui::Render(); }

void shutdownImgui() {
  ImGui_ImplVulkan_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();
}

} // namespace strafe::ui
