#pragma once

#include <volk.h>

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace strafe::gfx {

class Device;

// Fixed samplers, indexed in shaders by their value.
enum class Sampler : uint32_t {
  LinearRepeat,
  LinearClamp,
  NearestRepeat,
  NearestClamp,
  Count,
};

// The one global descriptor set. Every sampled image, storage image and
// sampler gets a slot in a big array, the set is bound once per command
// buffer, and shaders pick resources by slot index from push constants.
//
//   binding 0: texture2D sampled_images[]
//   binding 1: image2D   storage_images[]   (no users yet)
//   binding 2: sampler   samplers[]          (indexed by gfx::Sampler)
//
// Slots are partially bound, so unused ones may stay empty, and update after
// bind, so new images can be added while a frame using the set is in flight.
class BindlessSet {
public:
  static constexpr uint32_t kMaxSampledImages = 16384;
  static constexpr uint32_t kMaxStorageImages = 1024;

  static constexpr uint32_t kSampledImageBinding = 0;
  static constexpr uint32_t kStorageImageBinding = 1;
  static constexpr uint32_t kSamplerBinding = 2;

  BindlessSet() = default;
  ~BindlessSet() = default;

  BindlessSet(const BindlessSet &) = delete;
  BindlessSet &operator=(const BindlessSet &) = delete;

  bool initialize(Device &device);
  // Destroys immediately, so the GPU must be idle.
  void shutdown();

  VkDescriptorSetLayout layout() const { return layout_; }
  VkDescriptorSet set() const { return set_; }

  // Writes the view, which must be in SHADER_READ_ONLY_OPTIMAL whenever a
  // shader samples it, and returns its slot.
  std::optional<uint32_t> addSampledImage(VkImageView view);
  // The caller must be sure no submitted work still samples the slot, so call
  // it through FrameRing::deferDestroy together with destroying the image.
  void freeSampledImage(uint32_t slot);

private:
  bool createSamplers();

  Device *device_ = nullptr;
  VkDescriptorSetLayout layout_ = VK_NULL_HANDLE;
  VkDescriptorPool pool_ = VK_NULL_HANDLE;
  VkDescriptorSet set_ = VK_NULL_HANDLE;
  std::array<VkSampler, static_cast<size_t>(Sampler::Count)> samplers_{};

  uint32_t next_sampled_slot_ = 0;
  std::vector<uint32_t> free_sampled_slots_;
};

} // namespace strafe::gfx
