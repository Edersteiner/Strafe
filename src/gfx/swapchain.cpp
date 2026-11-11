#include "gfx/swapchain.h"

#include "gfx/device.h"
#include "gfx/vk_check.h"

#include <algorithm>

namespace strafe::gfx {

namespace {
VkExtent2D chooseSwapchainExtent(const VkSurfaceCapabilitiesKHR &caps,
                                 const VkExtent2D desired) {
  // If the current extent has a decided size already, use it.
  if (caps.currentExtent.width != UINT32_MAX) {
    return caps.currentExtent;
  }

  // If it doesn't, set extent based on our window.
  VkExtent2D extent = desired;

  // Clamp extent to what the driver allows
  extent.width = std::clamp(extent.width, caps.minImageExtent.width,
                            caps.maxImageExtent.width);
  extent.height = std::clamp(extent.height, caps.minImageExtent.height,
                             caps.maxImageExtent.height);

  return extent;
}
} // namespace

bool Swapchain::initialize(Device &device, VkExtent2D desired) {
  device_ = &device;
  if (!createSwapchain(desired, VK_NULL_HANDLE)) {
    return false;
  }
  // A window that starts minimized has no swapchain yet.
  require_recreate_ = (swapchain_handle_ == VK_NULL_HANDLE);
  return true;
}

void Swapchain::shutdown() {
  if (!device_) {
    return;
  }
  destroyImageResources();
  destroySwapchain();
}

bool Swapchain::recreate(VkExtent2D desired) {
  VK_CHECK(vkDeviceWaitIdle(device_->logicalDevice()));

  VkSwapchainKHR old_swapchain = swapchain_handle_;
  swapchain_handle_ = VK_NULL_HANDLE;

  destroyImageResources();

  const bool ok = createSwapchain(desired, old_swapchain);

  if (old_swapchain) {
    vkDestroySwapchainKHR(device_->logicalDevice(), old_swapchain, nullptr);
  }

  // Stays set at 0x0 so recreation is retried until the window is restored.
  require_recreate_ = !ok || swapchain_handle_ == VK_NULL_HANDLE;
  return ok;
}

bool Swapchain::createSwapchain(VkExtent2D desired,
                                VkSwapchainKHR old_swapchain) {
  VkSurfaceCapabilitiesKHR surface_caps{};
  if (!VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
          device_->physicalDevice(), device_->surface(), &surface_caps))) {
    return false;
  }

  VkExtent2D extent = chooseSwapchainExtent(surface_caps, desired);

  // If extent width or height is 0, the window is invisible or minimized.
  // Do not return a swapchain in that case
  if (extent.width == 0 || extent.height == 0) {
    swapchain_handle_ = VK_NULL_HANDLE;
    return true;
  }

  swapchain_width_ = extent.width;
  swapchain_height_ = extent.height;

  uint32_t requested_image_count = std::max(2u, surface_caps.minImageCount);
  if (surface_caps.maxImageCount > 0) {
    requested_image_count =
        std::min(requested_image_count, surface_caps.maxImageCount);
  }

  VkSwapchainCreateInfoKHR swapchain_create_info{
      .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
      .surface = device_->surface(),
      .minImageCount = requested_image_count,
      .imageFormat = swapchain_format_,
      .imageColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR,
      .imageExtent{.width = swapchain_width_, .height = swapchain_height_},
      .imageArrayLayers = 1,
      .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
      .preTransform = surface_caps.currentTransform,
      .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
      .presentMode = VK_PRESENT_MODE_MAILBOX_KHR,
      .oldSwapchain = old_swapchain};

  if (!VK_CHECK(vkCreateSwapchainKHR(device_->logicalDevice(),
                                     &swapchain_create_info, nullptr,
                                     &swapchain_handle_))) {
    return false;
  }

  // Get swapchain images
  uint32_t image_count = 0;
  if (!VK_CHECK(vkGetSwapchainImagesKHR(device_->logicalDevice(),
                                        swapchain_handle_, &image_count,
                                        nullptr))) {
    return false;
  }
  swapchain_images_.resize(image_count);
  if (!VK_CHECK(vkGetSwapchainImagesKHR(device_->logicalDevice(),
                                        swapchain_handle_, &image_count,
                                        swapchain_images_.data()))) {
    return false;
  }
  swapchain_image_views_.resize(image_count);

  for (size_t i = 0; i < swapchain_images_.size(); ++i) {
    VkImageViewCreateInfo img_view_info{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = swapchain_images_[i],
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = swapchain_format_,
        .subresourceRange{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                          .baseMipLevel = 0,
                          .levelCount = 1,
                          .baseArrayLayer = 0,
                          .layerCount = 1}};

    if (!VK_CHECK(vkCreateImageView(device_->logicalDevice(), &img_view_info,
                                    nullptr, &swapchain_image_views_[i]))) {
      return false;
    }
  }

  // Semaphores used to signal render completion
  render_complete_semaphores_.resize(swapchain_images_.size());
  for (VkSemaphore &semaphore : render_complete_semaphores_) {
    VkSemaphoreCreateInfo semaphore_info{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    if (!VK_CHECK(vkCreateSemaphore(device_->logicalDevice(), &semaphore_info,
                                    nullptr, &semaphore))) {
      return false;
    }
  }
  return true;
}

void Swapchain::destroyImageResources() {
  for (VkImageView swapchain_img_view : swapchain_image_views_) {
    vkDestroyImageView(device_->logicalDevice(), swapchain_img_view, nullptr);
  }
  swapchain_image_views_.clear();

  for (VkSemaphore &semaphore : render_complete_semaphores_) {
    vkDestroySemaphore(device_->logicalDevice(), semaphore, nullptr);
  }
  render_complete_semaphores_.clear();

  // Images are owned by swapchain, so clear vector but dont destroy
  swapchain_images_.clear();
}

void Swapchain::destroySwapchain() {
  if (swapchain_handle_) {
    vkDestroySwapchainKHR(device_->logicalDevice(), swapchain_handle_, nullptr);
    swapchain_handle_ = nullptr;
  }
}

} // namespace strafe::gfx
