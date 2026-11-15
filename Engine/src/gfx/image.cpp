#include "strafe/gfx/image.h"

#include "strafe/core/log.h"
#include "strafe/gfx/device.h"
#include "strafe/gfx/vk_check.h"

namespace strafe::gfx {

VkImageAspectFlags aspectFromFormat(VkFormat format) {
  switch (format) {
  case VK_FORMAT_D16_UNORM:
  case VK_FORMAT_X8_D24_UNORM_PACK32:
  case VK_FORMAT_D32_SFLOAT:
    return VK_IMAGE_ASPECT_DEPTH_BIT;
  case VK_FORMAT_S8_UINT:
    return VK_IMAGE_ASPECT_STENCIL_BIT;
  case VK_FORMAT_D16_UNORM_S8_UINT:
  case VK_FORMAT_D24_UNORM_S8_UINT:
  case VK_FORMAT_D32_SFLOAT_S8_UINT:
    return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
  default:
    return VK_IMAGE_ASPECT_COLOR_BIT;
  }
}

uint32_t texelSize(VkFormat format) {
  switch (format) {
  case VK_FORMAT_R8_UNORM:
    return 1;
  case VK_FORMAT_R8G8_UNORM:
    return 2;
  case VK_FORMAT_R8G8B8A8_UNORM:
  case VK_FORMAT_R8G8B8A8_SRGB:
  case VK_FORMAT_B8G8R8A8_UNORM:
  case VK_FORMAT_B8G8R8A8_SRGB:
    return 4;
  case VK_FORMAT_R16G16B16A16_SFLOAT:
    return 8;
  case VK_FORMAT_R32G32B32A32_SFLOAT:
    return 16;
  default:
    return 0;
  }
}

std::optional<Image> createImage(Device &device, const ImageDesc &desc) {
  if (desc.extent.width == 0 || desc.extent.height == 0) {
    log::error("Cannot create image of size {}x{}", desc.extent.width, desc.extent.height);
    return std::nullopt;
  }
  if (desc.format == VK_FORMAT_UNDEFINED || desc.usage == 0 || desc.mip_levels == 0) {
    log::error("Image needs a format, a usage and at least one mip level");
    return std::nullopt;
  }

  const VkImageCreateInfo image_info{
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .imageType = VK_IMAGE_TYPE_2D,
      .format = desc.format,
      .extent{.width = desc.extent.width, .height = desc.extent.height, .depth = 1},
      .mipLevels = desc.mip_levels,
      .arrayLayers = 1,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = desc.usage,
      .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};

  const VmaAllocationCreateInfo alloc_info{
      .flags = desc.dedicated ? VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT : VmaAllocationCreateFlags{0},
      .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};

  Image image{
      .format = desc.format,
      .aspect = aspectFromFormat(desc.format),
      .usage = desc.usage,
      .extent = desc.extent,
      .mip_levels = desc.mip_levels};
  if (!VK_CHECK(
          vmaCreateImage(device.allocator(), &image_info, &alloc_info, &image.handle, &image.allocation, nullptr))) {
    return std::nullopt;
  }

  const VkImageViewCreateInfo view_info{
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = image.handle,
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .format = desc.format,
      .subresourceRange{
          .aspectMask = image.aspect,
          .baseMipLevel = 0,
          .levelCount = desc.mip_levels,
          .baseArrayLayer = 0,
          .layerCount = 1}};
  if (!VK_CHECK(vkCreateImageView(device.logicalDevice(), &view_info, nullptr, &image.view))) {
    vmaDestroyImage(device.allocator(), image.handle, image.allocation);
    return std::nullopt;
  }

  return image;
}

void destroyImage(Device &device, const Image &image) {
  if (image.view) {
    vkDestroyImageView(device.logicalDevice(), image.view, nullptr);
  }
  if (image.handle) {
    vmaDestroyImage(device.allocator(), image.handle, image.allocation);
  }
}

} // namespace strafe::gfx
