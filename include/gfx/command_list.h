#pragma once

#include "gfx/buffer.h"

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

  // Temporary, for the barriers until step 6 adds transition().
  VkCommandBuffer handle() const { return buffer_; }

private:
  static constexpr uint32_t kMaxColorAttachments = 8;

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
