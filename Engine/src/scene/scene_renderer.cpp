#include "strafe/scene/scene_renderer.h"

#include "strafe/renderer.h"
#include "strafe/scene/scene.h"

#include <algorithm>

namespace strafe::scene {

void SceneRenderer::draw(const Scene &scene) {
  const renderer::MeshId cube = Renderer::primitive(renderer::Primitive::Cube);
  for (const Brush &brush : scene.brushes) {
    Renderer::draw({.mesh = cube, .material = materialFor(brush.color), .transform = brush.transform()});
  }
  for (const Object &object : scene.objects) {
    Renderer::renderObj(object.model_path, materialFor(object.color), object.transform);
  }
}

renderer::MaterialId SceneRenderer::materialFor(Vec4 color) {
  const auto it = std::ranges::find(materials_, color, &std::pair<Vec4, renderer::MaterialId>::first);
  if (it != materials_.end()) {
    return it->second;
  }

  if (!lit_) {
    lit_ = Renderer::loadShader("lit").value_or(gfx::ShaderId{});
  }
  renderer::MaterialId material;
  if (lit_->valid()) {
    material = Renderer::createMaterial(*lit_).value_or(renderer::MaterialId{});
  }
  if (material.valid()) {
    Renderer::setParam(material, "tint", color);
  }
  materials_.emplace_back(color, material);
  return material;
}

} // namespace strafe::scene
