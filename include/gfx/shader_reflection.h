#pragma once

#include "gfx/render_state.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace strafe::gfx {

// The field types a material struct may use. Texture and Sampler are the
// handle structs from strafe/bindless.slang, each a uint index.
enum class MaterialFieldType {
  Float,
  Float2,
  Float3,
  Float4,
  Int,
  UInt,
  Texture,
  Sampler,
};

constexpr std::string_view toString(MaterialFieldType type) {
  switch (type) {
  case MaterialFieldType::Float:
    return "float";
  case MaterialFieldType::Float2:
    return "float2";
  case MaterialFieldType::Float3:
    return "float3";
  case MaterialFieldType::Float4:
    return "float4";
  case MaterialFieldType::Int:
    return "int";
  case MaterialFieldType::UInt:
    return "uint";
  case MaterialFieldType::Texture:
    return "Texture";
  case MaterialFieldType::Sampler:
    return "Sampler";
  }
  return "unknown";
}

struct MaterialField {
  std::string name;
  MaterialFieldType type = MaterialFieldType::Float;
  // Byte offset inside the material block, in scalar layout.
  uint32_t offset = 0;
  // The field's initializer in the Slang struct, packed like the GPU reads
  // it. Empty without one, which means zero.
  std::vector<std::byte> default_value;
};

// The struct named by [Material("...")], as the GPU reads it.
struct MaterialLayout {
  std::vector<MaterialField> fields;
  uint32_t size = 0;

  const MaterialField *find(std::string_view name) const {
    for (const MaterialField &field : fields) {
      if (field.name == name) {
        return &field;
      }
    }
    return nullptr;
  }
};

// Everything the engine needs to know about a compiled shader.
struct ShaderReflection {
  std::string name;

  // From attributes, with their defaults when absent.
  BlendMode blend = BlendMode::Opaque;
  CullMode cull = CullMode::Back;
  DepthMode depth = DepthMode::TestWrite;
  std::string pass = "forward";
  // [Material("...")], empty when the shader has no material parameters.
  std::string material_type;
  // The layout of material_type, empty without one.
  MaterialLayout material;

  // SV_Target outputs of the fragment entry point.
  uint32_t color_outputs = 0;
  // Size of the push constant block, 0 when the shader doesn't use it.
  uint32_t push_constant_size = 0;
  // Size of what DrawPush::frame points to, 0 when there is no such field.
  uint32_t frame_data_size = 0;
};

} // namespace strafe::gfx
