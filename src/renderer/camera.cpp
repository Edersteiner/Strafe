#include "renderer/camera.h"

#include <glm/gtc/matrix_transform.hpp>

namespace strafe::renderer {

// glm builds matrices for OpenGL's Y-up clip space. Negating [1][1] flips Y,
// and passing the far plane as near reverses the depth range. Floats hold far
// more precision near 0, so reverse-Z spends it on distant geometry, where a
// standard projection packs depth values tightly together.

glm::mat4 perspective(float fov_y, float aspect, float near_plane,
                      float far_plane) {
  glm::mat4 projection =
      glm::perspectiveRH_ZO(fov_y, aspect, far_plane, near_plane);
  projection[1][1] *= -1.0f;
  return projection;
}

glm::mat4 orthographic(float height, float aspect, float near_plane,
                       float far_plane) {
  const float half_height = height / 2.0f;
  const float half_width = half_height * aspect;
  glm::mat4 projection = glm::orthoRH_ZO(-half_width, half_width, -half_height,
                                         half_height, far_plane, near_plane);
  projection[1][1] *= -1.0f;
  return projection;
}

} // namespace strafe::renderer
