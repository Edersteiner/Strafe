#pragma once

#include "strafe/core/types/vec2.h"
#include "strafe/core/types/vec3.h"
#include "strafe/gfx/buffer.h"

#include <cstdint>
#include <type_traits>
#include <vector>

namespace strafe::renderer {

struct Vertex {
  Vec3 position;
  Vec2 uv;
  Vec3 normal;
};

static_assert(std::is_trivially_copyable_v<Vertex>);
// shader.vert reads vertices with scalar layout, which packs them tightly.
static_assert(sizeof(Vertex) == 32);

struct MeshData {
  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
};

// Refers to a mesh created by Renderer::createMesh. The default is invalid, so
// a draw that never had its mesh set is caught instead of drawing mesh 0.
struct MeshId {
  uint32_t index = UINT32_MAX;

  bool valid() const { return index != UINT32_MAX; }
};

struct Mesh {
  gfx::Buffer vertex_buffer;
  gfx::Buffer index_buffer;
  uint32_t index_count = 0;
};

} // namespace strafe::renderer
