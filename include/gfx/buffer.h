#pragma once

#include <volk.h>
#include <vk_mem_alloc.h>

#include <cstddef>
#include <optional>
#include <span>

namespace strafe::gfx {

class Device;

struct Buffer {
  VkBuffer handle = VK_NULL_HANDLE;
  VmaAllocation allocation = VK_NULL_HANDLE;
  VkDeviceAddress address = 0;
  VkDeviceSize size = 0;
};

// Creates and returns a buffer, adds SHADER_DEVICE_ADDRESS so returned buffer
// always has a valid address.
std::optional<Buffer> createBuffer(Device &device, VkDeviceSize size,
                                   VkBufferUsageFlags usage);

// Copies and flushes. The range must fit inside the buffer, and the GPU must
// not be reading it.
bool writeBuffer(Device &device, const Buffer &buffer,
                 std::span<const std::byte> data, VkDeviceSize offset = 0);

// Destroys immeditely, so the gpu must be done with the buffer.
// Call it from FrameRing::deferDestroy.
void destroyBuffer(Device &device, const Buffer &buffer);

} // namespace strafe::gfx
