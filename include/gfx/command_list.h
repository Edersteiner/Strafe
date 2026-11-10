#pragma once

#include "gfx/buffer.h"
#include "gfx/image.h"
#include "gfx/resource_state.h"

#include <volk.h>

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <type_traits>

namespace strafe::gfx {

// Must match the push constant range in every pipeline layout.
inline constexpr VkShaderStageFlags kPushConstantStages =
    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

// Cleared on load and stored. The image must be in COLOR_ATTACHMENT_OPTIMAL.
struct ColorAttachment {
  VkImageView view = VK_NULL_HANDLE;
  glm::vec4 clear_color{0.0f, 0.0f, 0.0f, 1.0f};
};

// Cleared on load and discarded. The image must be in
// DEPTH_ATTACHMENT_OPTIMAL.
struct DepthAttachment {
  VkImageView view = VK_NULL_HANDLE;
  float clear_depth = 0.0f; // Reverse-Z: 0 is the far plane.
};

// Moves a whole image (every mip and layer) from one state to another. With
// discard the old contents are thrown away, which skips the layout conversion,
// but the barrier still waits for the previous use described by `from`.
// The aspect must match the image's format; gfx::Image carries it.
struct ImageTransition {
  VkImage image = VK_NULL_HANDLE;
  ResourceState from = ResourceState::Undefined;
  ResourceState to = ResourceState::Undefined;
  bool discard = false;
  VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
};

struct RenderingDesc {
  VkExtent2D extent{};
  std::span<const ColorAttachment> colors;
  std::optional<DepthAttachment> depth;
};

// Records into a command buffer that FrameRing has already begun. Owns
// nothing, and caches bound state, so create one per command buffer.
class CommandList {
public:
  explicit CommandList(VkCommandBuffer buffer);
  ~CommandList();

  CommandList(const CommandList &) = delete;
  CommandList &operator=(const CommandList &) = delete;

  // Barriers. Pass several transitions at once to record a single barrier.
  void transition(const ImageTransition &transition);
  void transition(std::span<const ImageTransition> transitions);

  // Copies. The image must be in TransferDst. Only mip 0 is written.
  void copyBuffer(const Buffer &src, const Buffer &dst, VkDeviceSize size,
                  VkDeviceSize src_offset = 0, VkDeviceSize dst_offset = 0);
  void copyBufferToImage(const Buffer &src, const Image &dst);

  // Rendering
  void beginRendering(const RenderingDesc &desc);
  void endRendering();

  // Dynamic state, covering the whole target with a [0, 1] depth range.
  void setViewport(VkExtent2D extent);
  void setScissor(VkExtent2D extent);

  // Pipeline and data
  void bindPipeline(VkPipeline pipeline, VkPipelineLayout layout);
  template <typename T> void pushConstants(const T &data);
  void bindIndexBuffer(const Buffer &buffer);

  // Draws
  void drawIndexed(uint32_t index_count, uint32_t instance_count = 1,
                   uint32_t first_index = 0, int32_t vertex_offset = 0,
                   uint32_t first_instance = 0);

private:
  static constexpr uint32_t kMaxColorAttachments = 8;
  static constexpr uint32_t kMaxTransitions = 16;

  void pushConstantsBytes(const void *data, uint32_t size);

  VkCommandBuffer buffer_ = VK_NULL_HANDLE;

  // Skips redundant binds within this command buffer.
  VkPipeline bound_pipeline_ = VK_NULL_HANDLE;
  VkPipelineLayout bound_layout_ = VK_NULL_HANDLE;
  VkBuffer bound_index_buffer_ = VK_NULL_HANDLE;

  bool inside_rendering_ = false;
};

template <typename T> void CommandList::pushConstants(const T &data) {
  static_assert(std::is_trivially_copyable_v<T>);
  static_assert(sizeof(T) % 4 == 0, "Vulkan requires a multiple of 4 bytes");
  pushConstantsBytes(&data, static_cast<uint32_t>(sizeof(T)));
}

} // namespace strafe::gfx
