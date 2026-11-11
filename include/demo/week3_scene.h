#pragma once

#include "renderer/frame_desc.h"
#include "renderer/material.h"
#include "renderer/mesh.h"

namespace strafe::renderer {
class Renderer;
} // namespace strafe::renderer

namespace strafe::demo {

struct Scene {
  renderer::MeshId redead;
  renderer::TextureId redead_texture;
  // Both use lit.slang: the plain texture, and a red see-through tint.
  renderer::MaterialId redead_material;
  renderer::MaterialId ghost_material;
  bool perspective = true;
  float time = 0.0f;
};

bool createSceneMeshes(renderer::Renderer &renderer, Scene &scene);

void toggleProjection(Scene &scene);

// Advances the animations.
void updateScene(Scene &scene, float delta_seconds);

void buildScene(renderer::FrameDesc &frame, const Scene &scene, float aspect);

} // namespace strafe::demo
