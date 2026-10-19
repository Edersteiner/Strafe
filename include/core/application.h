#pragma once

#include <SDL3/SDL.h>
#include <volk.h>

namespace strafe {
class Application {
public:
  bool initialize();
  void run();
  void shutdown();

private:
  const static uint32_t kVulkanVersion = VK_API_VERSION_1_4;

  bool initializeVulkan();
  bool createVulkanInstance();
  bool createSurface();
  VkPhysicalDevice findPhysicalDevice();
  bool findGraphicsQueue();
  bool createDevice(VkPhysicalDevice physical_device);

  void render();

  bool running_ = false;

  SDL_Window *window_ = nullptr;
  int width_ = 0, height_ = 0;

  VkInstance vulkan_instance_ = VK_NULL_HANDLE;

  VkSurfaceKHR surface_ = VK_NULL_HANDLE;

  VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;

  uint32_t gfx_queue_fam_idx_ = UINT32_MAX;
  VkQueue gfx_queue_ = VK_NULL_HANDLE;
};
} // namespace strafe
