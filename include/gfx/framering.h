#pragma once

#include <volk.h>

#include <cstdint>
#include <array>

namespace strafe::gfx {

class Device;

struct FrameResources {
  VkCommandPool command_pool = VK_NULL_HANDLE;
  VkCommandBuffer command_buffer = VK_NULL_HANDLE;
  VkSemaphore image_acquired_semaphore = VK_NULL_HANDLE;
};

struct FrameContext {
  VkCommandBuffer cmd = VK_NULL_HANDLE;
  uint32_t image_index = 0;
  uint64_t signal_value = 0;
};

class FrameRing {
public:
  FrameRing() = default;
  ~FrameRing() = default;

  FrameRing(const FrameRing&) = delete;
  FrameRing &operator=(const FrameRing&) = delete;

  bool initialize(Device& device);
  void shutdown();

  void render();

private:
  static constexpr uint32_t kMaxFramesInFlight = 2;

  Device* device_ = nullptr;

  bool createSyncResources();
  bool createCommandBuffers();

  uint32_t frame_index_ = 0;
  uint64_t next_signal_value_ = kMaxFramesInFlight + 1;

  VkSemaphore timeline_semaphore_ = VK_NULL_HANDLE;
  std::array<FrameResources, kMaxFramesInFlight> frame_resources_;
};

}
