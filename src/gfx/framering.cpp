#include "gfx/framering.h"

#include "core/log.h"
#include "gfx/device.h"
#include "gfx/swapchain.h"
#include "gfx/vk_check.h"

namespace strafe::gfx {

bool FrameRing::initialize(Device &device) {
  device_ = &device;

  if (!createSyncResources()) {
    log::error("Failed to create sync resources");
    return false;
  }
  if (!createCommandBuffers()) {
    log::error("Failed to create command buffers");
    return false;
  }

  return true;
}

void FrameRing::shutdown() {
  if (!device_) {
    return;
  }

  deletion_queue_.flushAll();

  if (timeline_semaphore_) {
    vkDestroySemaphore(device_->logicalDevice(), timeline_semaphore_, nullptr);
    timeline_semaphore_ = VK_NULL_HANDLE;
  }

  for (FrameResources &res : frame_resources_) {
    if (res.image_acquired_semaphore) {
      vkDestroySemaphore(device_->logicalDevice(), res.image_acquired_semaphore,
                         nullptr);
      res.image_acquired_semaphore = VK_NULL_HANDLE;
    }
    if (res.command_pool) {
      vkDestroyCommandPool(device_->logicalDevice(), res.command_pool, nullptr);
      res.command_pool = VK_NULL_HANDLE;
      res.command_buffer = VK_NULL_HANDLE;
    }
  }
}

std::optional<FrameContext> FrameRing::beginFrame(Swapchain &swapchain) {
  // No swapchain (window minimized, etc.) skip rendering.
  if (swapchain.handle() == VK_NULL_HANDLE) {
    return std::nullopt;
  }

  const uint32_t slot = frame_index_ % kMaxFramesInFlight;
  const uint64_t signal_value = next_signal_value_;
  const uint64_t wait_value = signal_value - kMaxFramesInFlight;

  VkSemaphoreWaitInfo wait_info{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
                                .semaphoreCount = 1,
                                .pSemaphores = &timeline_semaphore_,
                                .pValues = &wait_value};
  if (!VK_CHECK(
          vkWaitSemaphores(device_->logicalDevice(), &wait_info, UINT64_MAX))) {
    return std::nullopt;
  }

  uint64_t current_value = 0;
  if (VK_CHECK(vkGetSemaphoreCounterValue(
          device_->logicalDevice(), timeline_semaphore_, &current_value))) {
    deletion_queue_.flush(current_value);
  }

  FrameResources &res = frame_resources_[slot];
  if (!VK_CHECK(
          vkResetCommandPool(device_->logicalDevice(), res.command_pool, 0))) {
    return std::nullopt;
  }

  VkSemaphore image_acquire_semaphore = res.image_acquired_semaphore;

  uint32_t image_index = 0;
  VkResult acquire_result = vkAcquireNextImageKHR(
      device_->logicalDevice(), swapchain.handle(), UINT64_MAX,
      image_acquire_semaphore, VK_NULL_HANDLE, &image_index);

  if (acquire_result == VK_ERROR_OUT_OF_DATE_KHR) {
    swapchain.markForRecreate();
    return std::nullopt;
  }
  if (acquire_result == VK_SUBOPTIMAL_KHR) {
    swapchain.markForRecreate();
  } else if (!VK_CHECK(acquire_result)) {
    return std::nullopt;
  }

  ++frame_index_;
  ++next_signal_value_;

  VkCommandBufferBeginInfo cmd_begin_info{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};

  VK_CHECK(vkBeginCommandBuffer(res.command_buffer, &cmd_begin_info));

  return FrameContext{
      .cmd = res.command_buffer,
      .image_index = image_index,
      .frame_slot = slot,
      .signal_value = signal_value,
  };
}

bool FrameRing::createSyncResources() {
  VkSemaphoreTypeCreateInfo semaphore_type_info{
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
      .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
      .initialValue = kMaxFramesInFlight};
  VkSemaphoreCreateInfo timeline_semaphore_info{
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
      .pNext = &semaphore_type_info};
  if (!VK_CHECK(vkCreateSemaphore(device_->logicalDevice(),
                                  &timeline_semaphore_info, nullptr,
                                  &timeline_semaphore_))) {
    return false;
  }

  for (FrameResources &res : frame_resources_) {
    VkSemaphoreCreateInfo semaphore_info{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    if (!VK_CHECK(vkCreateSemaphore(device_->logicalDevice(), &semaphore_info,
                                    nullptr, &res.image_acquired_semaphore))) {
      return false;
    }
  }

  return true;
}

void FrameRing::endFrame(const FrameContext &ctx, Swapchain &swapchain) {
  VK_CHECK(vkEndCommandBuffer(ctx.cmd));

  FrameResources &res = frame_resources_[ctx.frame_slot];

  VkSemaphoreSubmitInfo image_acquire_wait_info{
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
      .semaphore = res.image_acquired_semaphore,
      .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
  };

  std::array<VkSemaphoreSubmitInfo, 2> semaphore_signals{
      {{// render work completion signal
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = swapchain.presentSemaphore(ctx.image_index),
        .stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT},
       {// entire frame is completed (timeline)
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = timeline_semaphore_,
        .value = ctx.signal_value,
        .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT}}};
  VkCommandBufferSubmitInfo cmd_submit_info{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
      .commandBuffer = ctx.cmd,
  };
  VkSubmitInfo2 submit_info{.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
                            .waitSemaphoreInfoCount = 1,
                            .pWaitSemaphoreInfos = &image_acquire_wait_info,
                            .commandBufferInfoCount = 1,
                            .pCommandBufferInfos = &cmd_submit_info,
                            .signalSemaphoreInfoCount =
                                static_cast<uint32_t>(semaphore_signals.size()),
                            .pSignalSemaphoreInfos = semaphore_signals.data()};
  VK_CHECK(vkQueueSubmit2(device_->graphicsQueue(), 1, &submit_info,
                          VK_NULL_HANDLE));

  const VkSemaphore present_semaphor =
      swapchain.presentSemaphore(ctx.image_index);
  const VkSwapchainKHR swapchain_handle = swapchain.handle();

  VkPresentInfoKHR present_info{.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
                                .waitSemaphoreCount = 1,
                                .pWaitSemaphores = &present_semaphor,
                                .swapchainCount = 1,
                                .pSwapchains = &swapchain_handle,
                                .pImageIndices = &ctx.image_index,
                                .pResults = nullptr};

  const VkResult present_result =
      vkQueuePresentKHR(device_->graphicsQueue(), &present_info);
  if (present_result == VK_ERROR_OUT_OF_DATE_KHR ||
      present_result == VK_SUBOPTIMAL_KHR) {
    swapchain.markForRecreate();
  } else {
    VK_CHECK(present_result);
  }
}

bool FrameRing::createCommandBuffers() {
  for (FrameResources &res : frame_resources_) {
    VkCommandPoolCreateInfo pool_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = device_->graphicsQueueFamily()};
    if (!VK_CHECK(vkCreateCommandPool(device_->logicalDevice(), &pool_info,
                                      nullptr, &res.command_pool))) {
      return false;
    }

    VkCommandBufferAllocateInfo cmd_alloc_info{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = res.command_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };

    if (!VK_CHECK(vkAllocateCommandBuffers(
            device_->logicalDevice(), &cmd_alloc_info, &res.command_buffer))) {
      return false;
    }
  }
  return true;
}

void FrameRing::deferDestroy(std::function<void()> destroy_fn) {
  // The value of next_signal_value_ - 1 is the most recently started frame
  deletion_queue_.push(next_signal_value_ - 1, std::move(destroy_fn));
}

} // namespace strafe::gfx
