#pragma once

#include "gfx/buffer.h"
#include "renderer/frame_desc.h"

#include <volk.h>
#include <vk_mem_alloc.h>

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

  void render(const gfx::FrameContext &ctx, const FrameDesc &desc);

private:
  static constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;

  bool createDepthBuffer(VkExtent2D extent);
  void destroyDepthBuffer();
  bool createShaders();
  bool createVertexBuffer();
  VkPipeline createGraphicsPipeline();

  gfx::Device *device_ = nullptr;
  gfx::Swapchain *swapchain_ = nullptr;
  gfx::FrameRing *frame_ring_ = nullptr;

  VkImage depth_image_ = VK_NULL_HANDLE;
  VkImageView depth_image_view_ = VK_NULL_HANDLE;
  VmaAllocation depth_image_allocation_ = VK_NULL_HANDLE;

  VkShaderModule vert_shader_ = VK_NULL_HANDLE;
  VkShaderModule frag_shader_ = VK_NULL_HANDLE;

  VkPipeline pipeline_ = VK_NULL_HANDLE;
  VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;

  gfx::Buffer vertex_buffer_;
};

} // namespace strafe::renderer
