#pragma once

#include <glm/glm.hpp>

namespace strafe::renderer {

// Both projections map the near plane to depth 1 and the far plane to depth 0
// (reverse-Z), and flip Y into Vulkan's Y-down clip space.

glm::mat4 perspective(float fov_y, float aspect, float near_plane, float far_plane);

// Height is the visible height in world units at any distance.
glm::mat4 orthographic(float height, float aspect, float near_plane, float far_plane);

} // namespace strafe::renderer
