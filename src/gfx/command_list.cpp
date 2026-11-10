#include "gfx/command_list.h"

#include <array>
#include <cassert>
#include <cstddef>

namespace strafe::gfx {

namespace {

struct StateInfo {
  VkImageLayout layout;
  VkPipelineStageFlags2 stages;
  VkAccessFlags2 access;
};

// The only place ResourceState meets Vulkan. As the source of a barrier a
// state says what to wait for, as the destination what has to wait.
StateInfo stateInfo(ResourceState state) {
  switch (state) {
  case ResourceState::Undefined:
    return {VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_NONE,
            VK_ACCESS_2_NONE};
  case ResourceState::Present:
    // The acquire semaphore is waited on at COLOR_ATTACHMENT_OUTPUT, so a
    // barrier out of Present must start there to run after the acquire. No
    // access, because the semaphores make presentation's reads visible.
    return {VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_NONE};
  case ResourceState::ColorTarget:
    return {VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |
                VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT};
  case ResourceState::DepthTarget:
    return {VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT};
  case ResourceState::TransferSrc:
    return {VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT};
  case ResourceState::TransferDst:
    return {VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_WRITE_BIT};
  case ResourceState::ShaderRead:
    return {VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT};
  }
  assert(false && "Unhandled ResourceState");
  return {VK_IMAGE_LAYOUT_UNDEFINED, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
          VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT};
}

// Todo: take the aspect from the image's format once images carry a desc.
// Until then a depth image can only be transitioned through DepthTarget.
VkImageAspectFlags aspectFor(const ImageTransition &transition) {
  const bool depth = transition.from == ResourceState::DepthTarget ||
                     transition.to == ResourceState::DepthTarget;
  return depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
}

} // namespace

CommandList::CommandList(VkCommandBuffer buffer) : buffer_(buffer) {
  assert(buffer_ && "CommandList needs a recording command buffer");
}

CommandList::~CommandList() {
  assert(!inside_rendering_ && "beginRendering without endRendering");
}

void CommandList::transition(const ImageTransition &transition) {
  this->transition(std::span<const ImageTransition>(&transition, 1));
}

void CommandList::transition(std::span<const ImageTransition> transitions) {
  assert(!inside_rendering_ && "Barriers can't be recorded inside rendering");
  assert(transitions.size() <= kMaxTransitions);
  if (transitions.empty()) {
    return;
  }

  std::array<VkImageMemoryBarrier2, kMaxTransitions> barriers{};
  for (std::size_t i = 0; i < transitions.size(); ++i) {
    const ImageTransition &t = transitions[i];
    assert(t.image && t.to != ResourceState::Undefined);
    const StateInfo src = stateInfo(t.from);
    const StateInfo dst = stateInfo(t.to);
    barriers[i] = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = src.stages,
        .srcAccessMask = src.access,
        .dstStageMask = dst.stages,
        .dstAccessMask = dst.access,
        .oldLayout = t.discard ? VK_IMAGE_LAYOUT_UNDEFINED : src.layout,
        .newLayout = dst.layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = t.image,
        .subresourceRange{.aspectMask = aspectFor(t),
                          .baseMipLevel = 0,
                          .levelCount = VK_REMAINING_MIP_LEVELS,
                          .baseArrayLayer = 0,
                          .layerCount = VK_REMAINING_ARRAY_LAYERS}};
  }

  const VkDependencyInfo dependency{
      .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
      .imageMemoryBarrierCount = static_cast<uint32_t>(transitions.size()),
      .pImageMemoryBarriers = barriers.data()};
  vkCmdPipelineBarrier2(buffer_, &dependency);
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
