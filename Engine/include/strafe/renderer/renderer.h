#pragma once

#include "strafe/gfx/bindless.h"
#include "strafe/gfx/buffer.h"
#include "strafe/gfx/framering.h"
#include "strafe/gfx/image.h"
#include "strafe/gfx/pipeline_manager.h"
#include "strafe/gfx/uploader.h"
#include "strafe/renderer/frame_desc.h"
#include "strafe/renderer/material.h"
#include "strafe/renderer/mesh.h"
#include "strafe/renderer/texture.h"

#include <volk.h>
#include <vk_mem_alloc.h>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace strafe::gfx {
class Device;
class Swapchain;
} // namespace strafe::gfx

namespace strafe::renderer {

class Renderer {
public:
  Renderer() = default;
  ~Renderer() = default;

  Renderer(const Renderer &) = delete;
  Renderer &operator=(const Renderer &) = delete;

  bool initialize(gfx::Device &device, gfx::Swapchain &swapchain, gfx::FrameRing &frame_ring);

  // Must run after vkDeviceWaitIdle and before FrameRing::shutdown, which
  // flushes the destruction deferred here.
  void shutdown();

  // Destroys the depth target immediately, so the GPU must be idle.
  // Swapchain::recreate guarantees that.
  bool resize(VkExtent2D extent);

  // Uploads the mesh once. The returned id stays valid until shutdown.
  std::optional<MeshId> createMesh(std::span<const Vertex> vertices, std::span<const uint32_t> indices);

  std::optional<MeshId> uploadMesh(Mesh mesh);

  // Uploads the texture once, without mipmaps. The returned id stays valid
  // until shutdown.
  std::optional<TextureId> createTexture(const TextureData &data, ColorSpace color_space);

  // 1x1 white, so an untextured draw keeps its plain color.
  TextureId defaultTexture() const { return default_texture_; }

  // Compiles shaders/<name>.slang and builds its pipeline.
  std::optional<gfx::ShaderId> loadShader(std::string_view name);

  std::optional<MaterialId> createMaterial(gfx::ShaderId shader);

  // Sets one material field by name.
  bool setParam(MaterialId material, std::string_view name, const MaterialParam &value);

  void render(const gfx::FrameContext &ctx, const FrameDesc &desc);

private:
  static constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;

  struct Texture {
    gfx::Image image;
    // Index into the bindless sampled image array, pushed with each draw.
    uint32_t bindless_slot = 0;
  };

  bool createDepthBuffer(VkExtent2D extent);
  void destroyDepthBuffer();
  bool createFrameBuffers();
  bool createPipelines();
  uint32_t textureSlot(TextureId id) const;
  void deferDestroyMesh(const Mesh &mesh);

  gfx::Device *device_ = nullptr;
  gfx::Swapchain *swapchain_ = nullptr;
  gfx::FrameRing *frame_ring_ = nullptr;

  gfx::Uploader uploader_;
  gfx::BindlessSet bindless_;

  gfx::Image depth_image_;

  // FrameData for each frame in flight, indexed by FrameContext::frame_slot.
  std::array<gfx::Buffer, gfx::FrameRing::kMaxFramesInFlight> frame_buffers_;

  gfx::PipelineManager pipelines_;
  MaterialTable materials_;
  // Indices into FrameDesc::draws in submission order.
  std::vector<uint32_t> draw_order_;

  std::vector<Mesh> meshes_;
  std::vector<Texture> textures_;
  TextureId default_texture_;
};

} // namespace strafe::renderer
