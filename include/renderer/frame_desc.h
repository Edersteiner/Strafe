#pragma once

#include "renderer/material.h"
#include "renderer/mesh.h"

#include <glm/glm.hpp>
#include <vector>

namespace strafe::renderer {

// The material picks the shader, and with it the pipeline. The renderer sorts
// draws by pipeline, their order here doesn't matter.
struct DrawItem {
  MeshId mesh;
  MaterialId material;
  glm::mat4 transform = glm::mat4(1.0f);
};

struct DirectionalLight {
  glm::vec3 direction{-0.4f, -1.0f, -0.6f};
  glm::vec3 color{1.0f, 1.0f, 1.0f};
  glm::vec3 ambient{0.12f, 0.13f, 0.16f};
};

struct FrameDesc {
  glm::mat4 view_projection = glm::mat4(1.0f);
  glm::vec3 camera_position{0.0f};
  DirectionalLight light;
  // Seconds since startup, for animated shaders.
  float time = 0.0f;
  std::vector<DrawItem> draws;
};

} // namespace strafe::renderer
