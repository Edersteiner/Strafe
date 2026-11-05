#pragma once

#include "renderer/mesh.h"

#include <glm/glm.hpp>
#include <vector>

namespace strafe::renderer {

struct DrawItem {
  MeshId mesh;
  glm::mat4 transform = glm::mat4(1.0f);
  glm::vec4 color = glm::vec4(1.0f);
};

struct FrameDesc {
  std::vector<DrawItem> draws;
};

} // namespace strafe::renderer
