#pragma once

#include "gfx/buffer.h"

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

struct MeshId {
  uint32_t index = 0;
};

} // namespace strafe::renderer
