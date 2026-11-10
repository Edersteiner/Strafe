#pragma once

#include "gfx/image.h"
#include "gfx/uploader.h"
#include "renderer/frame_desc.h"
#include "renderer/mesh.h"
#include "renderer/texture.h"

#include <volk.h>
#include <vk_mem_alloc.h>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace strafe::gfx {
class Device;
class FrameRing;
class Swapchain;
struct FrameContext;
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

  bool createDepthBuffer(VkExtent2D extent);
  void destroyDepthBuffer();
  bool createShaders();
  VkPipeline createGraphicsPipeline();
  void deferDestroyMesh(const Mesh &mesh);

  gfx::Device *device_ = nullptr;
  gfx::Swapchain *swapchain_ = nullptr;
  gfx::FrameRing *frame_ring_ = nullptr;

  gfx::Uploader uploader_;

  gfx::Image depth_image_;

  VkShaderModule vert_shader_ = VK_NULL_HANDLE;
  VkShaderModule frag_shader_ = VK_NULL_HANDLE;

  VkPipeline pipeline_ = VK_NULL_HANDLE;
  VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;

  std::vector<Mesh> meshes_;
  std::vector<gfx::Image> textures_;
  TextureId default_texture_;
};

} // namespace strafe::renderer
