#pragma once

#include <cstdint>

namespace strafe::gfx {

// How a resource is used. A barrier goes from one state to another, and the
// table in command_list.cpp maps each state to its layout, stages and access.
enum class ResourceState : uint8_t {
  Undefined,   // Nothing to wait for, contents are garbage.
  Present,     // Owned by the presentation engine.
  ColorTarget, // Color attachment, read and written by rendering.
  DepthTarget, // Depth attachment, read and written by the depth test.
  TransferSrc, // Source of a copy or blit.
  TransferDst, // Destination of a copy or blit.
  ShaderRead,  // Sampled in fragment or compute shaders.
};

} // namespace strafe::gfx
