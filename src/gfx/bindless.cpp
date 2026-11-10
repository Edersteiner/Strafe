#include "gfx/bindless.h"

#include "core/log.h"
#include "gfx/device.h"
#include "gfx/vk_check.h"

#include <cassert>

namespace strafe::gfx {

namespace {

constexpr uint32_t kSamplerCount = static_cast<uint32_t>(Sampler::Count);

VkSamplerCreateInfo samplerInfo(Sampler sampler, float max_anisotropy) {
  const bool linear =
      sampler == Sampler::LinearRepeat || sampler == Sampler::LinearClamp;
  const bool repeat =
      sampler == Sampler::LinearRepeat || sampler == Sampler::NearestRepeat;
  const VkFilter filter = linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
  const VkSamplerAddressMode address =
      repeat ? VK_SAMPLER_ADDRESS_MODE_REPEAT
             : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

  return {.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
          .magFilter = filter,
          .minFilter = filter,
          .mipmapMode = linear ? VK_SAMPLER_MIPMAP_MODE_LINEAR
                               : VK_SAMPLER_MIPMAP_MODE_NEAREST,
          .addressModeU = address,
          .addressModeV = address,
          .addressModeW = address,
          .mipLodBias = 0.0f,
          // Pixel art stays crisp, everything else gets anisotropic filtering.
          .anisotropyEnable = linear ? VK_TRUE : VK_FALSE,
          .maxAnisotropy = linear ? max_anisotropy : 1.0f,
          .compareEnable = VK_FALSE,
          .minLod = 0.0f,
          .maxLod = VK_LOD_CLAMP_NONE,
          .borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK};
}

} // namespace

bool BindlessSet::initialize(Device &device) {
  device_ = &device;
  const VkDevice vk_device = device_->logicalDevice();

  if (!createSamplers()) {
    return false;
  }

  const std::array<VkDescriptorSetLayoutBinding, 3> bindings{{
      {.binding = kSampledImageBinding,
       .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
       .descriptorCount = kMaxSampledImages,
       .stageFlags = VK_SHADER_STAGE_ALL},
      {.binding = kStorageImageBinding,
       .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
       .descriptorCount = kMaxStorageImages,
       .stageFlags = VK_SHADER_STAGE_ALL},
      {.binding = kSamplerBinding,
       .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
       .descriptorCount = kSamplerCount,
       .stageFlags = VK_SHADER_STAGE_ALL},
  }};
  // Samplers are written once before the set is ever bound, so they don't need
  // update after bind.
  const std::array<VkDescriptorBindingFlags, 3> binding_flags{
      VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
          VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,
      VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
          VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,
      VkDescriptorBindingFlags{0},
  };
  const VkDescriptorSetLayoutBindingFlagsCreateInfo flags_info{
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
      .bindingCount = static_cast<uint32_t>(binding_flags.size()),
      .pBindingFlags = binding_flags.data()};
  const VkDescriptorSetLayoutCreateInfo layout_info{
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .pNext = &flags_info,
      .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT,
      .bindingCount = static_cast<uint32_t>(bindings.size()),
      .pBindings = bindings.data()};
  if (!VK_CHECK(vkCreateDescriptorSetLayout(vk_device, &layout_info, nullptr,
                                            &layout_))) {
    return false;
  }

  const std::array<VkDescriptorPoolSize, 3> pool_sizes{{
      {.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
       .descriptorCount = kMaxSampledImages},
      {.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
       .descriptorCount = kMaxStorageImages},
      {.type = VK_DESCRIPTOR_TYPE_SAMPLER, .descriptorCount = kSamplerCount},
  }};
  const VkDescriptorPoolCreateInfo pool_info{
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      .flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT,
      .maxSets = 1,
      .poolSizeCount = static_cast<uint32_t>(pool_sizes.size()),
      .pPoolSizes = pool_sizes.data()};
  if (!VK_CHECK(vkCreateDescriptorPool(vk_device, &pool_info, nullptr,
                                       &pool_))) {
    return false;
  }

  const VkDescriptorSetAllocateInfo alloc_info{
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
      .descriptorPool = pool_,
      .descriptorSetCount = 1,
      .pSetLayouts = &layout_};
  if (!VK_CHECK(vkAllocateDescriptorSets(vk_device, &alloc_info, &set_))) {
    return false;
  }

  std::array<VkDescriptorImageInfo, kSamplerCount> sampler_infos{};
  for (uint32_t i = 0; i < kSamplerCount; ++i) {
    sampler_infos[i].sampler = samplers_[i];
  }
  const VkWriteDescriptorSet sampler_write{
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = set_,
      .dstBinding = kSamplerBinding,
      .dstArrayElement = 0,
      .descriptorCount = kSamplerCount,
      .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
      .pImageInfo = sampler_infos.data()};
  vkUpdateDescriptorSets(vk_device, 1, &sampler_write, 0, nullptr);

  return true;
}

void BindlessSet::shutdown() {
  if (!device_) {
    return;
  }
  const VkDevice vk_device = device_->logicalDevice();
  if (pool_) {
    // Frees set_ with it.
    vkDestroyDescriptorPool(vk_device, pool_, nullptr);
    pool_ = VK_NULL_HANDLE;
    set_ = VK_NULL_HANDLE;
  }
  if (layout_) {
    vkDestroyDescriptorSetLayout(vk_device, layout_, nullptr);
    layout_ = VK_NULL_HANDLE;
  }
  for (VkSampler &sampler : samplers_) {
    if (sampler) {
      vkDestroySampler(vk_device, sampler, nullptr);
      sampler = VK_NULL_HANDLE;
    }
  }
  next_sampled_slot_ = 0;
  free_sampled_slots_.clear();
  device_ = nullptr;
}

std::optional<uint32_t> BindlessSet::addSampledImage(VkImageView view) {
  assert(view);
  uint32_t slot = 0;
  if (!free_sampled_slots_.empty()) {
    slot = free_sampled_slots_.back();
    free_sampled_slots_.pop_back();
  } else if (next_sampled_slot_ < kMaxSampledImages) {
    slot = next_sampled_slot_++;
  } else {
    log::error("Out of bindless sampled image slots ({})", kMaxSampledImages);
    return std::nullopt;
  }

  const VkDescriptorImageInfo image_info{
      .imageView = view,
      .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
  const VkWriteDescriptorSet write{
      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
      .dstSet = set_,
      .dstBinding = kSampledImageBinding,
      .dstArrayElement = slot,
      .descriptorCount = 1,
      .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
      .pImageInfo = &image_info};
  vkUpdateDescriptorSets(device_->logicalDevice(), 1, &write, 0, nullptr);
  return slot;
}

void BindlessSet::freeSampledImage(uint32_t slot) {
  assert(slot < next_sampled_slot_);
  // The descriptor keeps pointing at the old view. That's fine because the
  // binding is partially bound and nothing indexes a freed slot.
  free_sampled_slots_.push_back(slot);
}

bool BindlessSet::createSamplers() {
  VkPhysicalDeviceProperties properties{};
  vkGetPhysicalDeviceProperties(device_->physicalDevice(), &properties);
  const float max_anisotropy = properties.limits.maxSamplerAnisotropy;

  for (uint32_t i = 0; i < kSamplerCount; ++i) {
    const VkSamplerCreateInfo info =
        samplerInfo(static_cast<Sampler>(i), max_anisotropy);
    if (!VK_CHECK(vkCreateSampler(device_->logicalDevice(), &info, nullptr,
                                  &samplers_[i]))) {
      return false;
    }
  }
  return true;
}

} // namespace strafe::gfx
