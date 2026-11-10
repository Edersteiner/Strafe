#pragma once

#include "renderer/frame_desc.h"
#include "renderer/mesh.h"

namespace strafe::renderer {
class Renderer;
} // namespace strafe::renderer

namespace strafe::demo {

struct Scene {
  renderer::MeshId redead;
  bool perspective = true;
  float time = 0.0f;
};

bool createSceneMeshes(renderer::Renderer &renderer, Scene &scene);

void toggleProjection(Scene &scene);

// Advances the animations.
void updateScene(Scene &scene, float delta_seconds);

void buildScene(renderer::FrameDesc &frame, const Scene &scene, float aspect);

} // namespace strafe::demo
