#pragma once

#include "gfx/buffer.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <glm/glm.hpp>

namespace strafe::renderer {

struct Vertex {
  glm::vec3 position;
  glm::vec3 color;
};

static_assert(sizeof(Vertex) == 24);
static_assert(offsetof(Vertex, position) == 0);
static_assert(offsetof(Vertex, color) == 12);
static_assert(std::is_trivially_copyable_v<Vertex>);

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
