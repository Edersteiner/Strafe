#pragma once

#include "strafe/core/types/vec4.h"
#include "strafe/gfx/pipeline_manager.h"
#include "strafe/renderer/material.h"

#include <optional>
#include <utility>
#include <vector>

namespace strafe::scene {

struct Scene;

// Turns a scene into draws, with the lit shader tinted by each color.
class SceneRenderer {
public:
  void draw(const Scene &scene);

private:
  // Invalid if the shader or material failed, which makes the draws skip.
  renderer::MaterialId materialFor(Vec4 color);

  std::optional<gfx::ShaderId> lit_;
  std::vector<std::pair<Vec4, renderer::MaterialId>> materials_;
};

} // namespace strafe::scene
