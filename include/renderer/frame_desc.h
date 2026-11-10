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

struct FrameDesc {
  glm::mat4 view_projection = glm::mat4(1.0f);
  std::vector<DrawItem> draws;
};

} // namespace strafe::renderer
