#pragma once

#include "vulkan/vulkan_core.h"
#include <SDL3/SDL.h>
#include <string>
#include <vector>
#include <volk.h>

// Include after volk. The implementation is compiled in src/core/vma.cpp.
#include <vk_mem_alloc.h>

// Todo: replace shaderc and glsl with Slang.
#include <shaderc/shaderc.hpp>

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
  bool initializeVMA();
  bool createSwapchain(int width, int height);
  void destroySwapchain();
  bool createShaders();
  VkShaderModule createShaderModule(const std::string &file_name, shaderc_shader_kind kind) const;
  VkPipeline createGraphicsPipeline();

  void render();

  bool running_ = false;

  SDL_Window *window_ = nullptr;
  int width_ = 1280, height_ = 720;

  VkInstance vulkan_instance_ = VK_NULL_HANDLE;

  VkSurfaceKHR surface_ = VK_NULL_HANDLE;

  VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;

  uint32_t gfx_queue_fam_idx_ = UINT32_MAX;
  VkQueue gfx_queue_ = VK_NULL_HANDLE;

  VmaAllocator vma_allocator_ = VK_NULL_HANDLE;

  VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
  std::vector<VkImage> swapchain_images_;
  std::vector<VkImageView> swapchain_image_views_;
  std::vector<VkSemaphore> render_complete_semaphores_;
  VkFormat swapchain_format_ = VK_FORMAT_B8G8R8A8_SRGB;
  bool require_swapchain_recreate = false;
  int swapchain_width_ = 0;
  int swapchain_height_ = 0;

  VkImage depth_image_ = VK_NULL_HANDLE;
  VkImageView depth_image_view = VK_NULL_HANDLE;
  VmaAllocation depth_image_allocation = VK_NULL_HANDLE;
  VkFormat depth_format_ = VK_FORMAT_D32_SFLOAT;

  VkShaderModule vert_shader = VK_NULL_HANDLE;
  VkShaderModule frag_shader = VK_NULL_HANDLE;

  VkPipeline pipeline_ = VK_NULL_HANDLE;
};
} // namespace strafe
