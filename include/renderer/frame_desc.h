#pragma once

#include "gfx/bindless.h"
#include "renderer/mesh.h"
#include "renderer/texture.h"

#include <glm/glm.hpp>
#include <vector>

namespace strafe::renderer {

struct DrawItem {
  MeshId mesh;
  glm::mat4 transform = glm::mat4(1.0f);
  // Multiplies the texture.
  glm::vec4 color = glm::vec4(1.0f);
  // Left invalid, the draw samples the 1x1 white default texture.
  TextureId texture;
  gfx::Sampler sampler = gfx::Sampler::LinearRepeat;
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
