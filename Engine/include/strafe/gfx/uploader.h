#pragma once

#include "strafe/gfx/buffer.h"
#include "strafe/gfx/image.h"
#include "strafe/gfx/resource_state.h"

#include <volk.h>

#include <cstddef>
#include <functional>
#include <optional>
#include <span>

namespace strafe::gfx {

class CommandList;
class Device;

// Blocking uploads into device local memory, for load time. Each call copies
// through a temporary staging buffer, submits on the graphics queue and waits
// for the GPU.
class Uploader {
public:
  Uploader() = default;
  ~Uploader() = default;

  Uploader(const Uploader &) = delete;
  Uploader &operator=(const Uploader &) = delete;

  bool initialize(Device &device);
  // Destroys immediately. Safe because every upload has finished on return.
  void shutdown();

  // Copies into a buffer created with BufferMemory::DeviceLocal and
  // TRANSFER_DST. Afterwards any later command may read it.
  bool uploadBuffer(const Buffer &dst, std::span<const std::byte> data, VkDeviceSize offset = 0);

  // Fills mip 0 from tightly packed texels and leaves the whole image in
  // `final_state`. The image needs TRANSFER_DST in its usage.
  bool uploadImage(
      const Image &dst, std::span<const std::byte> texels, ResourceState final_state = ResourceState::ShaderRead);

private:
  // Records through `record`, submits and waits.
  bool submitAndWait(const std::function<void(CommandList &)> &record);
  std::optional<Buffer> createStaging(std::span<const std::byte> data);

  Device *device_ = nullptr;
  VkCommandPool pool_ = VK_NULL_HANDLE;
  VkCommandBuffer cmd_ = VK_NULL_HANDLE;
  VkFence fence_ = VK_NULL_HANDLE;
};

} // namespace strafe::gfx
