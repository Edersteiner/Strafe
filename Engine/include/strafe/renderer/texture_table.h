#pragma once

#include "strafe/gfx/image.h"
#include "strafe/renderer/texture.h"

#include <volk.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace strafe::gfx {
class BindlessSet;
class Device;
class FrameRing;
class Uploader;
} // namespace strafe::gfx

namespace strafe::renderer {

// Every sampleable image, each with a slot in the bindless set. Ids are
// indices and are never reused, so a destroyed id stays invalid.
class TextureTable {
public:
  TextureTable() = default;
  ~TextureTable() = default;

  TextureTable(const TextureTable &) = delete;
  TextureTable &operator=(const TextureTable &) = delete;

  // Creates the default texture, so it needs the uploader ready.
  bool initialize(gfx::Device &device, gfx::FrameRing &frame_ring, gfx::Uploader &uploader, gfx::BindlessSet &bindless);
  // Destruction goes through the frame ring's deletion queue. The slots are
  // not freed, because the bindless set is destroyed right after.
  void shutdown();

  // Uploads the texture with a full mip chain.
  std::optional<TextureId> create(const TextureData &data, ColorSpace color_space);

  // An image the GPU renders into and later samples. It starts undefined.
  std::optional<TextureId> createTarget(VkExtent2D extent, VkFormat format);

  // Replaces the target's image and keeps its id. The bindless slot changes,
  // so materials sampling it must set the parameter again.
  bool resizeTarget(TextureId id, VkExtent2D extent);

  void destroy(TextureId id);

  // An invalid or destroyed id falls back to the default texture, because
  // sampling an empty slot is undefined behaviour on the GPU.
  uint32_t slot(TextureId id) const;

  // The id must be valid.
  const gfx::Image &image(TextureId id) const;

  bool valid(TextureId id) const { return id.index < textures_.size() && textures_[id.index].image.handle; }

  // 1x1 white, so an untextured draw keeps its plain color.
  TextureId defaultTexture() const { return default_texture_; }

private:
  struct Texture {
    gfx::Image image;
    // Index into the bindless sampled image array.
    uint32_t bindless_slot = 0;
    // Only targets can be resized, since uploaded textures have no data kept.
    bool target = false;
  };

  std::optional<TextureId> add(const gfx::Image &image, bool target);
  // Frees the slot and destroys the image once in flight frames are done.
  void deferDestroy(const Texture &texture);

  gfx::Device *device_ = nullptr;
  gfx::FrameRing *frame_ring_ = nullptr;
  gfx::Uploader *uploader_ = nullptr;
  gfx::BindlessSet *bindless_ = nullptr;

  std::vector<Texture> textures_;
  TextureId default_texture_;
};

} // namespace strafe::renderer
