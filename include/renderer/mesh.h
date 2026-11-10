#pragma once

#include "gfx/buffer.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

#include <glm/glm.hpp>

namespace strafe::renderer {

struct Vertex {
  glm::vec3 position;
  glm::vec2 uv;
  glm::vec3 normal;
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
