#include "strafe/scene/scene_renderer.h"

#include "strafe/renderer/frame_desc.h"
#include "strafe/renderer/primitives.h"
#include "strafe/renderer/renderer.h"
#include "strafe/scene/scene.h"

#include <algorithm>

namespace strafe::scene {

void SceneRenderer::draw(renderer::Renderer &renderer, const Scene &scene, renderer::FrameDesc &frame) {
  const renderer::MeshId cube = renderer.primitive(renderer::Primitive::Cube);
  for (const Brush &brush : scene.brushes) {
    frame.draws.push_back({.mesh = cube, .material = materialFor(renderer, brush.color), .transform = brush.transform()});
  }
  for (const Object &object : scene.objects) {
    renderer.renderObj(frame, object.model_path, materialFor(renderer, object.color), object.transform);
  }
}

renderer::MaterialId SceneRenderer::materialFor(renderer::Renderer &renderer, Vec4 color) {
  const auto it = std::ranges::find(materials_, color, &std::pair<Vec4, renderer::MaterialId>::first);
  if (it != materials_.end()) {
    return it->second;
  }

  if (!lit_) {
    lit_ = renderer.loadShader("lit").value_or(gfx::ShaderId{});
  }
  renderer::MaterialId material;
  if (lit_->valid()) {
    material = renderer.createMaterial(*lit_).value_or(renderer::MaterialId{});
  }
  if (material.valid()) {
    renderer.setParam(material, "tint", color);
  }
  materials_.emplace_back(color, material);
  return material;
}

} // namespace strafe::scene
