#include "strafe/renderer/texture_table.h"

#include "strafe/core/log.h"
#include "strafe/gfx/bindless.h"
#include "strafe/gfx/device.h"
#include "strafe/gfx/framering.h"
#include "strafe/gfx/uploader.h"

#include <cassert>
#include <cstddef>

namespace strafe::renderer {

bool TextureTable::initialize(
    gfx::Device &device, gfx::FrameRing &frame_ring, gfx::Uploader &uploader, gfx::BindlessSet &bindless) {
  device_ = &device;
  frame_ring_ = &frame_ring;
  uploader_ = &uploader;
  bindless_ = &bindless;

  const TextureData white{
      .width = 1, .height = 1, .rgba = {std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff}}};
  const std::optional<TextureId> id = create(white, ColorSpace::Srgb);
  if (!id) {
    log::error("Failed to create the default texture");
    return false;
  }
  default_texture_ = *id;
  return true;
}

void TextureTable::shutdown() {
  if (!device_) {
    return;
  }
  for (const Texture &texture : textures_) {
    if (texture.image.handle) {
      frame_ring_->deferDestroy([gfx_device = device_, image = texture.image] { gfx::destroyImage(*gfx_device, image); });
    }
  }
  textures_.clear();
  default_texture_ = {};
  device_ = nullptr;
}

std::optional<TextureId> TextureTable::create(const TextureData &data, ColorSpace color_space) {
  const VkExtent2D extent{.width = data.width, .height = data.height};
  // TRANSFER_SRC because the uploader blits each mip from the one above.
  const std::optional<gfx::Image> image = gfx::createImage(
      *device_,
      {.extent = extent,
       .format = color_space == ColorSpace::Srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM,
       .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
       .mip_levels = gfx::fullMipCount(extent)});
  if (!image) {
    log::error("Failed to create a {}x{} texture", data.width, data.height);
    return std::nullopt;
  }

  // Validates the byte count against the extent and format.
  if (!uploader_->uploadImage(*image, data.rgba)) {
    // Nothing on the GPU uses it, and the uploader has finished with it.
    gfx::destroyImage(*device_, *image);
    return std::nullopt;
  }
  return add(*image, false);
}

std::optional<TextureId> TextureTable::createTarget(VkExtent2D extent, VkFormat format) {
  const std::optional<gfx::Image> image = gfx::createImage(
      *device_,
      {.extent = extent,
       .format = format,
       .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
       .dedicated = true});
  if (!image) {
    log::error("Failed to create a {}x{} render target", extent.width, extent.height);
    return std::nullopt;
  }
  return add(*image, true);
}

bool TextureTable::resizeTarget(TextureId id, VkExtent2D extent) {
  if (!valid(id) || !textures_[id.index].target) {
    log::error("resizeTarget needs a texture from createTarget");
    return false;
  }
  Texture &texture = textures_[id.index];

  const std::optional<gfx::Image> image = gfx::createImage(
      *device_,
      {.extent = extent, .format = texture.image.format, .usage = texture.image.usage, .dedicated = true});
  if (!image) {
    log::error("Failed to resize a render target to {}x{}", extent.width, extent.height);
    return false;
  }
  const std::optional<uint32_t> slot = bindless_->addSampledImage(image->view);
  if (!slot) {
    gfx::destroyImage(*device_, *image);
    return false;
  }

  // The old image may still be sampled by a frame in flight.
  deferDestroy(texture);
  texture.image = *image;
  texture.bindless_slot = *slot;
  return true;
}

void TextureTable::destroy(TextureId id) {
  if (!valid(id)) {
    return;
  }
  if (id.index == default_texture_.index) {
    log::error("The default texture can't be destroyed");
    return;
  }
  deferDestroy(textures_[id.index]);
  textures_[id.index] = {};
}

uint32_t TextureTable::slot(TextureId id) const {
  const TextureId resolved = valid(id) ? id : default_texture_;
  return textures_[resolved.index].bindless_slot;
}

const gfx::Image &TextureTable::image(TextureId id) const {
  assert(valid(id));
  return textures_[id.index].image;
}

std::optional<TextureId> TextureTable::add(const gfx::Image &image, bool target) {
  const std::optional<uint32_t> slot = bindless_->addSampledImage(image.view);
  if (!slot) {
    gfx::destroyImage(*device_, image);
    return std::nullopt;
  }
  textures_.push_back({.image = image, .bindless_slot = *slot, .target = target});
  return TextureId{.index = static_cast<uint32_t>(textures_.size() - 1)};
}

void TextureTable::deferDestroy(const Texture &texture) {
  frame_ring_->deferDestroy([gfx_device = device_, bindless = bindless_, texture] {
    bindless->freeSampledImage(texture.bindless_slot);
    gfx::destroyImage(*gfx_device, texture.image);
  });
}

} // namespace strafe::renderer
