#pragma once

#include <vk_mem_alloc.h>
#include <volk.h>

#include <vector>

namespace strafe::gfx {

class Device;

class Swapchain {
public:
  Swapchain() = default;
  ~Swapchain() = default;

  Swapchain(const Swapchain &) = delete;
  Swapchain &operator=(const Swapchain &) = delete;

  bool initialize(Device &device, VkExtent2D desired);
  void shutdown();

  bool recreate(VkExtent2D desired);

  VkSwapchainKHR handle() const { return swapchain_handle_; }
  VkImage image(uint32_t index) const { return swapchain_images_[index]; }
  VkImageView imageView(uint32_t index) const {
    return swapchain_image_views_[index];
  }
  VkSemaphore presentSemaphore(uint32_t index) const {
    return render_complete_semaphores_[index];
  }
  VkExtent2D extent() const {
    return VkExtent2D{.width = swapchain_width_, .height = swapchain_height_};
  }
  VkFormat format() const { return swapchain_format_; }

  void markForRecreate() { require_recreate_ = true; }
  bool requireRecreate() const { return require_recreate_; }

private:
  bool createSwapchain(VkExtent2D desired, VkSwapchainKHR oldSwapchain);
  void destroyImageResources();
  void destroySwapchain();

  Device *device_ = nullptr;

  VkSwapchainKHR swapchain_handle_ = VK_NULL_HANDLE;
  std::vector<VkImage> swapchain_images_;
  std::vector<VkImageView> swapchain_image_views_;
  std::vector<VkSemaphore> render_complete_semaphores_;

  uint32_t swapchain_width_ = 0;
  uint32_t swapchain_height_ = 0;

  VkFormat swapchain_format_ = VK_FORMAT_B8G8R8A8_SRGB;

  bool require_recreate_ = false;
};
} // namespace strafe::gfx
