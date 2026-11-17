#pragma once

#include "strafe/core/math.h"
#include "strafe/core/types/mat4.h"
#include "strafe/core/types/transform.h"
#include "strafe/core/types/vec3.h"

#include <variant>

namespace strafe::renderer {

struct Perspective {
  float fov_y = radians(70.0f);
  float near_plane = 0.1f;
  float far_plane = 500.0f;
};

struct Orthographic {
  float height = 10.0f;
  float near_plane = 0.1f;
  float far_plane = 500.0f;
};

using Projection = std::variant<Perspective, Orthographic>;

// Looks down its local -Z with +Y up, so an unrotated camera sees what lies
// at negative Z. The transform's scale is ignored.
struct Camera {
  Transform transform;
  Projection projection = Perspective{};

  // Turns to face target from the current position.
  void lookAt(Vec3 target, Vec3 up = Vec3(0.0f, 1.0f, 0.0f));
};

// Moves the world so the camera sits at the origin looking down -Z.
Mat4 viewMatrix(const Camera &camera);

Mat4 projectionMatrix(const Projection &projection, float aspect);

// Both projections map the near plane to depth 1 and the far plane to depth 0
// (reverse-Z), and flip Y into Vulkan's Y-down clip space.

Mat4 perspective(float fov_y, float aspect, float near_plane, float far_plane);

// Height is the visible height in world units at any distance.
Mat4 orthographic(float height, float aspect, float near_plane, float far_plane);

} // namespace strafe::renderer
