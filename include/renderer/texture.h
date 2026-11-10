#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace strafe::renderer {

// Decoded pixels on the CPU, always 8 bit RGBA, rows top to bottom.
struct TextureData {
  uint32_t width = 0;
  uint32_t height = 0;
  std::vector<std::byte> rgba;
};

// How the GPU interprets the stored values when sampling.
enum class ColorSpace {
  // Colors authored by eye: albedo, UI. Sampling converts to linear.
  Srgb,
  // Data: normal maps, roughness, masks. Sampled as is.
  Linear,
};

// Refers to a texture created by Renderer::createTexture. The default is
// invalid, like MeshId.
struct TextureId {
  uint32_t index = UINT32_MAX;

  bool valid() const { return index != UINT32_MAX; }
};

} // namespace strafe::renderer
