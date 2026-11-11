#pragma once

#include "gfx/bindless.h"
#include "gfx/buffer.h"
#include "gfx/framering.h"
#include "gfx/image.h"
#include "gfx/pipeline_manager.h"
#include "gfx/uploader.h"
#include "renderer/frame_desc.h"
#include "renderer/mesh.h"
#include "renderer/texture.h"

#include <volk.h>
#include <vk_mem_alloc.h>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
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

  bool initialize(gfx::Device &device, gfx::Swapchain &swapchain,
                  gfx::FrameRing &frame_ring);

  // Must run after vkDeviceWaitIdle and before FrameRing::shutdown, which
  // flushes the destruction deferred here.
  void shutdown();

  // Destroys the depth target immediately, so the GPU must be idle.
  // Swapchain::recreate guarantees that.
  bool resize(VkExtent2D extent);

  // Uploads the mesh once. The returned id stays valid until shutdown.
  std::optional<MeshId> createMesh(std::span<const Vertex> vertices,
                                   std::span<const uint32_t> indices);

  std::optional<MeshId> uploadMesh(Mesh mesh);

  // Uploads the texture once, without mipmaps. The returned id stays valid
  // until shutdown.
  std::optional<TextureId> createTexture(const TextureData &data,
                                         ColorSpace color_space);

  // 1x1 white, so an untextured draw keeps its plain color.
  TextureId defaultTexture() const { return default_texture_; }

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
  // Todo:  draws pick their shader through their material.
  gfx::ShaderId lit_shader_;

  std::vector<Mesh> meshes_;
  std::vector<Texture> textures_;
  TextureId default_texture_;
};

} // namespace strafe::renderer
