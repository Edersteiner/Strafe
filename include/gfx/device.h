#pragma once

#include <volk.h>
#include <vk_mem_alloc.h>

struct SDL_Window;

namespace strafe::gfx {

// Abstracts Instance, Device, VMA, Queue and Surface creation and usage.
class Device {
public:
  Device() = default;
  ~Device() = default;

  Device(const Device&) = delete;
  Device &operator=(const Device&) = delete;

  bool initialize(SDL_Window* window, VkFormat requested_swapchain_format);
  void shutdown();

  VkDevice logicalDevice() const {return logical_device_; }
  VkPhysicalDevice physicalDevice() const { return physical_device_; }
  VkSurfaceKHR surface() const { return surface_; }
  VmaAllocator allocator() const { return vma_allocator_; }
  VkQueue graphicsQueue() const { return graphics_queue_; }
  uint32_t graphicsQueueFamily() const { return graphics_queue_family_idx_; }

private:
  static constexpr uint32_t kVulkanVersion = VK_API_VERSION_1_4;

  bool initializeVMA();
  bool createVulkanInstance();

  bool createSurface(SDL_Window* window);

  bool findPhysicalDevice(VkFormat requested_swapchain_format);
  bool findGraphicsQueue();

  bool createLogicalDevice();

  VkInstance instance_ = VK_NULL_HANDLE;
  VmaAllocator vma_allocator_ = VK_NULL_HANDLE;

  VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
  VkDevice logical_device_ = VK_NULL_HANDLE;

  uint32_t graphics_queue_family_idx_ = UINT32_MAX;
  VkQueue graphics_queue_ = VK_NULL_HANDLE;

  VkSurfaceKHR surface_ = VK_NULL_HANDLE;

  VkDebugUtilsMessengerEXT debug_messenger_ = VK_NULL_HANDLE;
};

}
