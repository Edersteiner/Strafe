#pragma once

#include "strafe/core/types/transform.h"
#include "strafe/core/types/vec3.h"
#include "strafe/renderer/material.h"
#include "strafe/renderer/mesh.h"
#include "strafe/renderer/view.h"

#include <vector>

namespace strafe::renderer {

// The material picks the shader, and with it the pipeline. The renderer sorts
// draws by pipeline, their order here doesn't matter.
struct DrawItem {
  MeshId mesh;
  MaterialId material;
  Transform transform;
};

struct DirectionalLight {
  Vec3 direction{-0.4f, -1.0f, -0.6f};
  Vec3 color{1.0f};
  Vec3 ambient{0.12f, 0.13f, 0.16f};
};

struct FrameDesc {
  // Only used when views is empty, as one view covering the whole window.
  Camera camera;
  // Drawn in order, later views over earlier ones where regions overlap.
  std::vector<ViewDesc> views;
  DirectionalLight light;
  // Seconds since startup, for animated shaders.
  float time = 0.0f;
  std::vector<DrawItem> draws;
};

} // namespace strafe::renderer
