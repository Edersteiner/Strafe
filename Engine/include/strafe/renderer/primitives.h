#pragma once

#include "strafe/renderer/mesh.h"

#include <cstdint>

namespace strafe::renderer {

enum class Primitive : uint8_t {
  Cube,
  Sphere,
  Plane,
  Count,
};

MeshData cubeMeshData();
MeshData sphereMeshData(uint32_t segments = 32, uint32_t rings = 16);
MeshData planeMeshData();

MeshData primitiveMeshData(Primitive primitive);

} // namespace strafe::renderer
