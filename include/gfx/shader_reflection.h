#pragma once

#include "gfx/render_state.h"

#include <cstdint>
#include <string>

namespace strafe::gfx {

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

  // SV_Target outputs of the fragment entry point.
  uint32_t color_outputs = 0;
  // Size of the push constant block, 0 when the shader doesn't use it.
  uint32_t push_constant_size = 0;
};

} // namespace strafe::gfx
