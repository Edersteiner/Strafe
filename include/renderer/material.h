#pragma once

#include "gfx/bindless.h"
#include "gfx/buffer.h"
#include "gfx/framering.h"
#include "gfx/pipeline_manager.h"
#include "renderer/texture.h"

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace strafe::gfx {
class Device;
} // namespace strafe::gfx

namespace strafe::renderer {

struct MaterialId {
  uint32_t index = UINT32_MAX;

  bool valid() const { return index != UINT32_MAX; }
};

// Value from a shader material struct.
using MaterialParam = std::variant<float, glm::vec2, glm::vec3, glm::vec4,
                                   int32_t, uint32_t, TextureId, gfx::Sampler>;

// Every material's parameters, each packed into a block with the layout
// reflected from its shader.
class MaterialTable {
public:
  // Turns a texture into its bindless slot when packing.
  using TextureSlotFn = std::function<uint32_t(TextureId)>;

  MaterialTable() = default;
  ~MaterialTable() = default;

  MaterialTable(const MaterialTable &) = delete;
  MaterialTable &operator=(const MaterialTable &) = delete;

  void initialize(gfx::Device &device, gfx::FrameRing &frame_ring,
                  const gfx::PipelineManager &pipelines,
                  TextureSlotFn texture_slot);
  // Destruction goes through the frame ring's deletion queue.
  void shutdown();

  // Every field starts zeroed, except textures, which start as whatever
  // TextureSlotFn returns for an invalid id.
  MaterialId create(gfx::ShaderId shader);

  // Fails when material, parameter name or value is invalid.
  bool set(MaterialId id, std::string_view name, const MaterialParam &value);

  bool valid(MaterialId id) const { return id.index < materials_.size(); }
  gfx::ShaderId shader(MaterialId id) const;

  // Brings this frame's buffer up to date.
  bool upload(uint32_t frame_slot);

  // The material's block in this frame's buffer, 0 when its shader has none
  VkDeviceAddress address(MaterialId id, uint32_t frame_slot) const;

private:
  static constexpr uint32_t kMaterialAlignment = 16;
  static constexpr uint32_t kFrames = gfx::FrameRing::kMaxFramesInFlight;

  struct Material {
    gfx::ShaderId shader;
    uint32_t offset = 0;
    // Kept so hot reload can re-pack by name into a changed layout.
    std::unordered_map<std::string, MaterialParam> values;
  };

  void pack(const Material &material, const gfx::MaterialField &field,
            const MaterialParam &value);

  gfx::Device *device_ = nullptr;
  gfx::FrameRing *frame_ring_ = nullptr;
  const gfx::PipelineManager *pipelines_ = nullptr;
  TextureSlotFn texture_slot_;

  std::vector<Material> materials_;
  std::vector<std::byte> data_;

  std::array<gfx::Buffer, kFrames> buffers_{};
  std::array<bool, kFrames> dirty_{};
};

} // namespace strafe::renderer
