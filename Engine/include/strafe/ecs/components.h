#pragma once

#include "strafe/renderer/material.h"
#include "strafe/renderer/mesh.h"

#include <string>

namespace strafe {

// What Registry::find and the console know an entity by.
struct [[component]] Name {
  std::string value;
};

// Draws the mesh with the material at the entity's Transform.
struct MeshRenderer {
  renderer::MeshId mesh;
  renderer::MaterialId material;
};

} // namespace strafe

#include "strafe/ecs/components.gen.h"
