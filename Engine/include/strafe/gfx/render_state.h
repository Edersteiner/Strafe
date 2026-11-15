#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace strafe::gfx {

// Render state a shader picks through its attributes. Mapped to Vulkan in
// pipeline_manager.cpp and command_list.cpp.

// [Blend("...")]. Baked into the pipeline.
enum class BlendMode : uint8_t {
  Opaque,   // "opaque", no blending (default)
  Alpha,    // "alpha", src * a + dst * (1 - a)
  Additive, // "additive", src * a + dst
};

// [Cull("...")]. Dynamic state, set when the shader is bound.
enum class CullMode : uint8_t {
  Back,  // "back" (default)
  Front, // "front"
  None,  // "none"
};

// [Depth("...")]. Dynamic state. The compare op is always GREATER for
// reverse-Z.
enum class DepthMode : uint8_t {
  TestWrite, // "test_write" (default)
  Test,      // "test", for transparent surfaces
  Off,       // "off"
};

std::optional<BlendMode> parseBlendMode(std::string_view text);
std::optional<CullMode> parseCullMode(std::string_view text);
std::optional<DepthMode> parseDepthMode(std::string_view text);

} // namespace strafe::gfx
