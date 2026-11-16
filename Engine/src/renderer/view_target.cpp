#include "strafe/renderer/view_target.h"

#include "strafe/core/log.h"
#include "strafe/gfx/device.h"
#include "strafe/gfx/framering.h"
#include "strafe/renderer/texture_table.h"

namespace strafe::renderer {

namespace {

std::optional<gfx::Image> createDepth(gfx::Device &device, VkExtent2D extent) {
  return gfx::createImage(
      device,
      {.extent = extent,
       .format = kViewDepthFormat,
       .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
       .dedicated = true});
}

void deferDestroyDepth(gfx::Device &device, gfx::FrameRing &frame_ring, const gfx::Image &depth) {
  if (depth.handle) {
    frame_ring.deferDestroy([gfx_device = &device, depth] { gfx::destroyImage(*gfx_device, depth); });
  }
}

} // namespace

std::optional<ViewTarget> createViewTarget(gfx::Device &device, TextureTable &textures, VkExtent2D extent) {
  const std::optional<TextureId> color = textures.createTarget(extent, kViewColorFormat);
  if (!color) {
    return std::nullopt;
  }
  const std::optional<gfx::Image> depth = createDepth(device, extent);
  if (!depth) {
    log::error("Failed to create a {}x{} view depth target", extent.width, extent.height);
    textures.destroy(*color);
    return std::nullopt;
  }
  return ViewTarget{.color = *color, .depth = *depth, .extent = extent};
}

bool resizeViewTarget(
    gfx::Device &device, gfx::FrameRing &frame_ring, TextureTable &textures, ViewTarget &target, VkExtent2D extent) {
  const std::optional<gfx::Image> depth = createDepth(device, extent);
  if (!depth) {
    log::error("Failed to resize a view depth target to {}x{}", extent.width, extent.height);
    return false;
  }
  if (!textures.resizeTarget(target.color, extent)) {
    gfx::destroyImage(device, *depth);
    return false;
  }
  deferDestroyDepth(device, frame_ring, target.depth);
  target.depth = *depth;
  target.extent = extent;
  return true;
}

void destroyViewTarget(gfx::Device &device, gfx::FrameRing &frame_ring, TextureTable &textures, ViewTarget &target) {
  textures.destroy(target.color);
  deferDestroyDepth(device, frame_ring, target.depth);
  target = {};
}

} // namespace strafe::renderer
