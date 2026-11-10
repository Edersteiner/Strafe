#include "gfx/buffer.h"

#include "core/log.h"
#include "gfx/device.h"
#include "gfx/vk_check.h"

#include <cassert>

namespace strafe::gfx {

std::optional<Buffer> createBuffer(Device &device, VkDeviceSize size,
                                   VkBufferUsageFlags usage,
                                   BufferMemory memory) {
  if (size <= 0) {
    log::error("Cannot create buffer of size 0");
    return std::nullopt;
  }

  VkBufferCreateInfo buffer_info{
      .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      .size = size,
      .usage = usage | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
  };

  const bool host_write = memory == BufferMemory::HostWrite;
  VmaAllocationCreateInfo alloc_info{
      .flags = host_write
                   ? VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
                   : VmaAllocationCreateFlags{0},
      .usage = host_write ? VMA_MEMORY_USAGE_AUTO
                          : VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
  };

  Buffer new_buffer{};
  if (!VK_CHECK(vmaCreateBuffer(device.allocator(), &buffer_info, &alloc_info,
                                &new_buffer.handle, &new_buffer.allocation,
                                nullptr))) {
    return std::nullopt;
  }

  VkBufferDeviceAddressInfo address_info{
      .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
      .buffer = new_buffer.handle,
  };

  new_buffer.address =
      vkGetBufferDeviceAddress(device.logicalDevice(), &address_info);

  new_buffer.size = size;

  return new_buffer;
}

bool writeBuffer(Device &device, const Buffer &buffer,
                 std::span<const std::byte> data, VkDeviceSize offset) {
  if (data.empty()) {
    log::debug("Nothing to write");
    return true;
  }
  if (!(data.size() <= buffer.size && offset <= buffer.size - data.size())) {
    log::error(
        "Buffer write out of bounds: buffer size {}, offset {}, data size {}",
        buffer.size, offset, data.size());
    return false;
  }

  return VK_CHECK(vmaCopyMemoryToAllocation(
      device.allocator(), data.data(), buffer.allocation, offset, data.size()));
}

void destroyBuffer(Device &device, const Buffer &buffer) {
  vmaDestroyBuffer(device.allocator(), buffer.handle, buffer.allocation);
}

} // namespace strafe::gfx
