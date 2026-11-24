#pragma once

#include "strafe/core/types/mat4.h"
#include "strafe/core/types/quat.h"
#include "strafe/core/types/vec3.h"

namespace strafe {

// Position, rotation and scale.
struct [[reflect]] Transform {
  Vec3 position;
  Quat rotation;
  Vec3 scale{1.0f};

  // Scales first, then rotates, then moves to position.
  Mat4 toMatrix() const;
};

} // namespace strafe

#include "strafe/core/types/transform.gen.h"
