#include "gfx/command_list.h"

#include <array>
#include <cassert>
#include <cstddef>

namespace strafe::gfx {

CommandList::CommandList(VkCommandBuffer buffer) : buffer_(buffer) {
  assert(buffer_ && "CommandList needs a recording command buffer");
}

CommandList::~CommandList() {
  assert(!inside_rendering_ && "beginRendering without endRendering");
}

void CommandList::beginRendering(const RenderingDesc &desc) {
  assert(!inside_rendering_ && "Already inside a rendering pass");
  assert(desc.colors.size() <= kMaxColorAttachments);

  std::array<VkRenderingAttachmentInfo, kMaxColorAttachments> colors{};
  for (std::size_t i = 0; i < desc.colors.size(); ++i) {
    const glm::vec4 &clear = desc.colors[i].clear_color;
    colors[i] = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = desc.colors[i].view,
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue{.color{.float32 = {clear.r, clear.g, clear.b, clear.a}}}};
  }

  VkRenderingAttachmentInfo depth{};
  if (desc.depth) {
    depth = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
             .imageView = desc.depth->view,
             .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
             .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
             .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
             .clearValue{.depthStencil{.depth = desc.depth->clear_depth,
                                       .stencil = 0}}};
  }

  const VkRenderingInfo rendering_info{
      .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
      .renderArea{.offset{.x = 0, .y = 0}, .extent = desc.extent},
      .layerCount = 1,
      .colorAttachmentCount = static_cast<uint32_t>(desc.colors.size()),
      .pColorAttachments = colors.data(),
      .pDepthAttachment = desc.depth ? &depth : nullptr};

  vkCmdBeginRendering(buffer_, &rendering_info);
  inside_rendering_ = true;
}

void CommandList::endRendering() {
  assert(inside_rendering_ && "endRendering without beginRendering");
  vkCmdEndRendering(buffer_);
  inside_rendering_ = false;
}

void CommandList::setViewport(VkExtent2D extent) {
  const VkViewport viewport{
      .x = 0.0f,
      .y = 0.0f,
      .width = static_cast<float>(extent.width),
      .height = static_cast<float>(extent.height),
      .minDepth = 0.0f,
      .maxDepth = 1.0f,
  };
  vkCmdSetViewport(buffer_, 0, 1, &viewport);
}

void CommandList::setScissor(VkExtent2D extent) {
  const VkRect2D scissor{.offset{.x = 0, .y = 0}, .extent = extent};
  vkCmdSetScissor(buffer_, 0, 1, &scissor);
}

void CommandList::bindPipeline(VkPipeline pipeline, VkPipelineLayout layout) {
  assert(pipeline && layout);
  if (pipeline == bound_pipeline_) {
    return;
  }
  vkCmdBindPipeline(buffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
  bound_pipeline_ = pipeline;
  bound_layout_ = layout;
}

void CommandList::pushConstantsBytes(const void *data, uint32_t size) {
  assert(bound_layout_ && "bindPipeline before pushConstants");
  vkCmdPushConstants(buffer_, bound_layout_, kPushConstantStages, 0, size,
                     data);
}

void CommandList::bindIndexBuffer(const Buffer &buffer) {
  assert(buffer.handle);
  if (buffer.handle == bound_index_buffer_) {
    return;
  }
  vkCmdBindIndexBuffer(buffer_, buffer.handle, 0, VK_INDEX_TYPE_UINT32);
  bound_index_buffer_ = buffer.handle;
}

void CommandList::drawIndexed(uint32_t index_count, uint32_t instance_count,
                              uint32_t first_index, int32_t vertex_offset,
                              uint32_t first_instance) {
  assert(inside_rendering_ && "drawIndexed outside a rendering pass");
  vkCmdDrawIndexed(buffer_, index_count, instance_count, first_index,
                   vertex_offset, first_instance);
}

} // namespace strafe::gfx
