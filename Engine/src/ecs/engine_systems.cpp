#include "ecs/engine_systems.h"

#include "strafe/renderer.h"

namespace strafe::ecs {

void drawMeshes(const Transform &transform, const MeshRenderer &mesh_renderer) {
  // A MeshRenderer added without its mesh or material yet draws nothing.
  if (mesh_renderer.mesh.valid() && mesh_renderer.material.valid()) {
    Renderer::draw({.mesh = mesh_renderer.mesh, .material = mesh_renderer.material, .transform = transform});
  }
}

} // namespace strafe::ecs
