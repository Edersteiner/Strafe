#include "strafe/gfx/uploader.h"

#include "strafe/core/log.h"
#include "strafe/gfx/command_list.h"
#include "strafe/gfx/device.h"
#include "strafe/gfx/vk_check.h"

#include <cstdint>

namespace strafe::gfx {

namespace {

// Mip generation blits the image into itself with linear filtering.
bool canGenerateMips(const Image &image, VkPhysicalDevice physical_device) {
  if (!(image.usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)) {
    log::error("Generating mips needs an image with TRANSFER_SRC usage");
    return false;
  }
  VkFormatProperties properties{};
  vkGetPhysicalDeviceFormatProperties(physical_device, image.format, &properties);
  constexpr VkFormatFeatureFlags kNeeded = VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT |
      VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
  if ((properties.optimalTilingFeatures & kNeeded) != kNeeded) {
    log::error(
        "Format {} can't be blitted with linear filtering, so mips can't be generated", static_cast<int>(image.format));
    return false;
  }
  return true;
}

} // namespace

bool Uploader::initialize(Device &device) {
  device_ = &device;
  const VkDevice vk_device = device_->logicalDevice();

  const VkCommandPoolCreateInfo pool_info{
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
      .queueFamilyIndex = device_->graphicsQueueFamily()};
  if (!VK_CHECK(vkCreateCommandPool(vk_device, &pool_info, nullptr, &pool_))) {
    return false;
  }

  const VkCommandBufferAllocateInfo alloc_info{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = pool_,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1};
  if (!VK_CHECK(vkAllocateCommandBuffers(vk_device, &alloc_info, &cmd_))) {
    return false;
  }

  const VkFenceCreateInfo fence_info{.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  return VK_CHECK(vkCreateFence(vk_device, &fence_info, nullptr, &fence_));
}

void Uploader::shutdown() {
  if (!device_) {
    return;
  }
  const VkDevice vk_device = device_->logicalDevice();
  if (fence_) {
    vkDestroyFence(vk_device, fence_, nullptr);
    fence_ = VK_NULL_HANDLE;
  }
  if (pool_) {
    // Frees cmd_ with it.
    vkDestroyCommandPool(vk_device, pool_, nullptr);
    pool_ = VK_NULL_HANDLE;
    cmd_ = VK_NULL_HANDLE;
  }
  device_ = nullptr;
}

bool Uploader::uploadBuffer(const Buffer &dst, std::span<const std::byte> data, VkDeviceSize offset) {
  if (data.empty()) {
    return true;
  }
  if (!dst.handle || data.size() > dst.size || offset > dst.size - data.size()) {
    log::error("Buffer upload out of bounds: buffer size {}, offset {}, data size {}", dst.size, offset, data.size());
    return false;
  }

  std::optional<Buffer> staging = createStaging(data);
  if (!staging) {
    return false;
  }

  const bool ok = submitAndWait([&](CommandList &cmd) { cmd.copyBuffer(*staging, dst, data.size(), 0, offset); });
  destroyBuffer(*device_, *staging);
  return ok;
}

bool Uploader::uploadImage(const Image &dst, std::span<const std::byte> texels, ResourceState final_state) {
  if (!dst.handle || !(dst.usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT)) {
    log::error("Image upload needs an image with TRANSFER_DST usage");
    return false;
  }
  const uint32_t texel_size = texelSize(dst.format);
  if (texel_size == 0) {
    log::error("Image upload doesn't support format {}", static_cast<int>(dst.format));
    return false;
  }
  const std::size_t expected = static_cast<std::size_t>(dst.extent.width) * dst.extent.height * texel_size;
  if (texels.size() != expected) {
    log::error(
        "Image upload expected {} bytes for {}x{}, got {}", expected, dst.extent.width, dst.extent.height,
        texels.size());
    return false;
  }

  if (dst.mip_levels > 1 && !canGenerateMips(dst, device_->physicalDevice())) {
    return false;
  }

  std::optional<Buffer> staging = createStaging(texels);
  if (!staging) {
    return false;
  }

  const bool ok = submitAndWait([&](CommandList &cmd) {
    cmd.transition(
        {.image = dst.handle,
         .from = ResourceState::Undefined,
         .to = ResourceState::TransferDst,
         .aspect = dst.aspect});
    cmd.copyBufferToImage(*staging, dst);

    // Each mip is finished once written, so it turns into the source for the next.
    for (uint32_t mip = 1; mip < dst.mip_levels; ++mip) {
      cmd.transition(
          {.image = dst.handle,
           .from = ResourceState::TransferDst,
           .to = ResourceState::TransferSrc,
           .aspect = dst.aspect,
           .base_mip = mip - 1,
           .mip_count = 1});
      cmd.blitMip(dst, mip);
    }

    // Every mip but the last was a blit source. These barriers also make the
    // writes visible to everything that later reads the image in final_state,
    // in this or any later submission.
    const uint32_t last_mip = dst.mip_levels - 1;
    if (last_mip > 0) {
      cmd.transition(
          {.image = dst.handle,
           .from = ResourceState::TransferSrc,
           .to = final_state,
           .aspect = dst.aspect,
           .base_mip = 0,
           .mip_count = last_mip});
    }
    cmd.transition(
        {.image = dst.handle,
         .from = ResourceState::TransferDst,
         .to = final_state,
         .aspect = dst.aspect,
         .base_mip = last_mip,
         .mip_count = 1});
  });
  destroyBuffer(*device_, *staging);
  return ok;
}

bool Uploader::submitAndWait(const std::function<void(CommandList &)> &record) {
  const VkDevice vk_device = device_->logicalDevice();
  if (!VK_CHECK(vkResetCommandPool(vk_device, pool_, 0))) {
    return false;
  }

  const VkCommandBufferBeginInfo begin_info{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
  if (!VK_CHECK(vkBeginCommandBuffer(cmd_, &begin_info))) {
    return false;
  }

  {
    CommandList cmd(cmd_);
    record(cmd);
  }

  // Waiting on the fence only tells the host the copies finished. This
  // barrier is what makes the transfer writes visible to the vertex pulling
  // and index reads of later frames.
  const VkMemoryBarrier2 written{
      .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
      .srcStageMask = VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT,
      .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
      .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
      .dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT};
  const VkDependencyInfo dependency{
      .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .memoryBarrierCount = 1, .pMemoryBarriers = &written};
  vkCmdPipelineBarrier2(cmd_, &dependency);

  if (!VK_CHECK(vkEndCommandBuffer(cmd_))) {
    return false;
  }

  const VkCommandBufferSubmitInfo cmd_info{
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, .commandBuffer = cmd_};
  const VkSubmitInfo2 submit{
      .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2, .commandBufferInfoCount = 1, .pCommandBufferInfos = &cmd_info};
  if (!VK_CHECK(vkQueueSubmit2(device_->graphicsQueue(), 1, &submit, fence_))) {
    return false;
  }

  const bool ok = VK_CHECK(vkWaitForFences(vk_device, 1, &fence_, VK_TRUE, UINT64_MAX));
  VK_CHECK(vkResetFences(vk_device, 1, &fence_));
  return ok;
}

std::optional<Buffer> Uploader::createStaging(std::span<const std::byte> data) {
  std::optional<Buffer> staging =
      createBuffer(*device_, data.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, BufferMemory::HostWrite);
  if (!staging) {
    return std::nullopt;
  }
  if (!writeBuffer(*device_, *staging, data)) {
    destroyBuffer(*device_, *staging);
    return std::nullopt;
  }
  return staging;
}

} // namespace strafe::gfx
