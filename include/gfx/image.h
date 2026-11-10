#pragma once

#include <volk.h>
#include <vk_mem_alloc.h>

#include <cstdint>
#include <optional>

namespace strafe::gfx {

class Device;

// A 2D image with one array layer. Grows into ResourceManager's ImageDesc.
struct ImageDesc {
  VkExtent2D extent{};
  VkFormat format = VK_FORMAT_UNDEFINED;
  VkImageUsageFlags usage = 0;
  uint32_t mip_levels = 1;
  // Gives the image its own memory block. Worth it for large render targets
  // that are recreated on resize, wasteful for small textures.
  bool dedicated = false;
};

struct Image {
  VkImage handle = VK_NULL_HANDLE;
  // Covers every mip level.
  VkImageView view = VK_NULL_HANDLE;
  VmaAllocation allocation = VK_NULL_HANDLE;
  VkFormat format = VK_FORMAT_UNDEFINED;
  VkImageAspectFlags aspect = 0;
  VkExtent2D extent{};
  uint32_t mip_levels = 0;
};

// The aspect a view or barrier of this format needs.
VkImageAspectFlags aspectFromFormat(VkFormat format);

// Creates the image in device local memory together with a view over all of
// it. The image starts in ResourceState::Undefined.
std::optional<Image> createImage(Device &device, const ImageDesc &desc);

// Destroys immediately, so the GPU must be done with the image.
// Call it from FrameRing::deferDestroy.
void destroyImage(Device &device, const Image &image);

} // namespace strafe::gfx
