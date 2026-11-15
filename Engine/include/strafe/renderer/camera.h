#pragma once

#include "strafe/core/types/mat4.h"

namespace strafe::renderer {

// Both projections map the near plane to depth 1 and the far plane to depth 0
// (reverse-Z), and flip Y into Vulkan's Y-down clip space.

Mat4 perspective(float fov_y, float aspect, float near_plane, float far_plane);

// Height is the visible height in world units at any distance.
Mat4 orthographic(float height, float aspect, float near_plane, float far_plane);

} // namespace strafe::renderer
