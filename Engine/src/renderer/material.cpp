#include "strafe/renderer/material.h"

#include "strafe/core/log.h"
#include "strafe/gfx/device.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <optional>
#include <type_traits>

namespace strafe::renderer {

namespace {

gfx::MaterialFieldType fieldType(const MaterialParam &value) {
  return std::visit(
      [](const auto &v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, float>) {
          return gfx::MaterialFieldType::Float;
        } else if constexpr (std::is_same_v<T, glm::vec2>) {
          return gfx::MaterialFieldType::Float2;
        } else if constexpr (std::is_same_v<T, glm::vec3>) {
          return gfx::MaterialFieldType::Float3;
        } else if constexpr (std::is_same_v<T, glm::vec4>) {
          return gfx::MaterialFieldType::Float4;
        } else if constexpr (std::is_same_v<T, int32_t>) {
          return gfx::MaterialFieldType::Int;
        } else if constexpr (std::is_same_v<T, uint32_t>) {
          return gfx::MaterialFieldType::UInt;
        } else if constexpr (std::is_same_v<T, TextureId>) {
          return gfx::MaterialFieldType::Texture;
        } else {
          static_assert(std::is_same_v<T, gfx::Sampler>);
          return gfx::MaterialFieldType::Sampler;
        }
      },
      value);
}

uint32_t alignUp(uint32_t value, uint32_t alignment) { return (value + alignment - 1) / alignment * alignment; }

} // namespace

void MaterialTable::initialize(
    gfx::Device &device, gfx::FrameRing &frame_ring, const gfx::PipelineManager &pipelines,
    TextureSlotFn texture_slot) {
  device_ = &device;
  frame_ring_ = &frame_ring;
  pipelines_ = &pipelines;
  texture_slot_ = std::move(texture_slot);
}

void MaterialTable::shutdown() {
  if (!device_) {
    return;
  }
  for (gfx::Buffer &buffer : buffers_) {
    if (buffer.handle) {
      frame_ring_->deferDestroy([device = device_, buffer] { gfx::destroyBuffer(*device, buffer); });
    }
    buffer = {};
  }
  materials_.clear();
  data_.clear();
  device_ = nullptr;
}

MaterialId MaterialTable::create(gfx::ShaderId shader) {
  const gfx::MaterialLayout &layout = pipelines_->reflection(shader).material;

  Material material{.shader = shader, .offset = alignUp(static_cast<uint32_t>(data_.size()), kMaterialAlignment)};
  data_.resize(material.offset + layout.size, std::byte{0});

  for (const gfx::MaterialField &field : layout.fields) {
    if (field.type == gfx::MaterialFieldType::Texture) {
      // Slot 0 isn't necessarily the white texture, so textures are packed
      // explicitly, and a Slang initializer can't name a loaded texture.
      pack(material, field, TextureId{});
    } else if (!field.default_value.empty()) {
      std::memcpy(
          data_.data() + material.offset + field.offset, field.default_value.data(), field.default_value.size());
    }
  }

  materials_.push_back(std::move(material));
  dirty_.fill(true);
  return MaterialId{.index = static_cast<uint32_t>(materials_.size() - 1)};
}

bool MaterialTable::set(MaterialId id, std::string_view name, const MaterialParam &value) {
  if (!valid(id)) {
    log::error("setParam(\"{}\") on an invalid material", name);
    return false;
  }
  Material &material = materials_[id.index];
  const gfx::ShaderReflection &reflection = pipelines_->reflection(material.shader);

  const gfx::MaterialField *field = reflection.material.find(name);
  if (!field) {
    if (reflection.material_type.empty()) {
      log::error("setParam(\"{}\"): shader {} has no [Material]", name, reflection.name);
    } else {
      log::error(
          "setParam(\"{}\"): {} in shader {} has no field {}", name, reflection.material_type, reflection.name, name);
    }
    return false;
  }
  if (fieldType(value) != field->type) {
    log::error(
        "setParam(\"{}\"): {}.{} is a {}, but the value is a {}", name, reflection.material_type, name,
        gfx::toString(field->type), gfx::toString(fieldType(value)));
    return false;
  }

  material.values.insert_or_assign(std::string(name), value);
  pack(material, *field, value);
  dirty_.fill(true);
  return true;
}

void MaterialTable::pack(const Material &material, const gfx::MaterialField &field, const MaterialParam &value) {
  std::byte *dst = data_.data() + material.offset + field.offset;
  std::visit(
      [&](const auto &v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, TextureId>) {
          const uint32_t slot = texture_slot_(v);
          std::memcpy(dst, &slot, sizeof(slot));
        } else if constexpr (std::is_same_v<T, gfx::Sampler>) {
          const auto index = static_cast<uint32_t>(v);
          std::memcpy(dst, &index, sizeof(index));
        } else {
          // glm vectors are tightly packed floats, matching scalar layout.
          std::memcpy(dst, &v, sizeof(v));
        }
      },
      value);
}

gfx::ShaderId MaterialTable::shader(MaterialId id) const {
  assert(valid(id) && "Invalid MaterialId");
  return materials_[id.index].shader;
}

bool MaterialTable::upload(uint32_t frame_slot) {
  assert(frame_slot < kFrames);
  if (!dirty_[frame_slot] || data_.empty()) {
    return true;
  }

  gfx::Buffer &buffer = buffers_[frame_slot];
  if (buffer.size < data_.size()) {
    // Doubles in size, to avoid reallocating each frame.
    if (buffer.handle) {
      frame_ring_->deferDestroy([device = device_, old = buffer] { gfx::destroyBuffer(*device, old); });
      buffer = {};
    }
    const VkDeviceSize size = std::max<VkDeviceSize>(data_.size() * 2, VkDeviceSize{1024});
    const std::optional<gfx::Buffer> created = gfx::createBuffer(*device_, size, 0);
    if (!created) {
      log::error("Failed to grow the material buffer to {} bytes", size);
      return false;
    }
    buffer = *created;
  }

  // The whole table, which is fine while materials are few and rarely change.
  if (!gfx::writeBuffer(*device_, buffer, data_)) {
    return false;
  }
  dirty_[frame_slot] = false;
  return true;
}

VkDeviceAddress MaterialTable::address(MaterialId id, uint32_t frame_slot) const {
  assert(valid(id) && "Invalid MaterialId");
  const Material &material = materials_[id.index];
  if (pipelines_->reflection(material.shader).material.size == 0) {
    return 0;
  }
  return buffers_[frame_slot].address + material.offset;
}

} // namespace strafe::renderer
