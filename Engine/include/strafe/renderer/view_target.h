#pragma once

#include "strafe/gfx/image.h"
#include "strafe/renderer/texture.h"

#include <volk.h>

#include <optional>

namespace strafe::gfx {
class Device;
class FrameRing;
} // namespace strafe::gfx

namespace strafe::renderer {

class TextureTable;

inline constexpr VkFormat kViewColorFormat = VK_FORMAT_R8G8B8A8_SRGB;
inline constexpr VkFormat kViewDepthFormat = VK_FORMAT_D32_SFLOAT;

// The images one view renders the scene into. The color image is a texture,
// so the compositor and materials can sample it.
struct ViewTarget {
  TextureId color;
  gfx::Image depth;
  VkExtent2D extent{};
};

std::optional<ViewTarget> createViewTarget(gfx::Device &device, TextureTable &textures, VkExtent2D extent);

// Keeps the color id, but its bindless slot changes, so materials sampling it
// must set the parameter again. Old images are destroyed once in flight
// frames are done with them.
bool resizeViewTarget(
    gfx::Device &device, gfx::FrameRing &frame_ring, TextureTable &textures, ViewTarget &target, VkExtent2D extent);

void destroyViewTarget(gfx::Device &device, gfx::FrameRing &frame_ring, TextureTable &textures, ViewTarget &target);

} // namespace strafe::renderer
