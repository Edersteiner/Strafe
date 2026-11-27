#pragma once

#include "strafe/core/types/transform.h"
#include "strafe/ecs/components.h"

namespace strafe::ecs {

// Draws every MeshRenderer at its entity's Transform.
[[system(stage = "render")]] void drawMeshes(const Transform &transform, const MeshRenderer &mesh_renderer);

} // namespace strafe::ecs
