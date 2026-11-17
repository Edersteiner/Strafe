#include "strafe/renderer/camera.h"

#include "core/glm_interop.h"

#include <glm/gtc/matrix_transform.hpp>

#include <type_traits>

namespace strafe::renderer {

Mat4 perspective(float fov_y, float aspect, float near_plane, float far_plane) {
  glm::mat4 projection = glm::perspectiveRH_ZO(fov_y, aspect, far_plane, near_plane);
  projection[1][1] *= -1.0f;
  return fromGlm(projection);
}

Mat4 orthographic(float height, float aspect, float near_plane, float far_plane) {
  const float half_height = height / 2.0f;
  const float half_width = half_height * aspect;
  glm::mat4 projection = glm::orthoRH_ZO(-half_width, half_width, -half_height, half_height, far_plane, near_plane);
  projection[1][1] *= -1.0f;
  return fromGlm(projection);
}

void Camera::lookAt(Vec3 target, Vec3 up) { transform.rotation = lookRotation(target - transform.position, up); }

Mat4 viewMatrix(const Camera &camera) {
  return rotation(inverse(camera.transform.rotation)) * translation(-camera.transform.position);
}

Mat4 projectionMatrix(const Projection &projection, float aspect) {
  return std::visit(
      [aspect](const auto &p) {
        using T = std::decay_t<decltype(p)>;
        if constexpr (std::is_same_v<T, Perspective>) {
          return perspective(p.fov_y, aspect, p.near_plane, p.far_plane);
        } else {
          static_assert(std::is_same_v<T, Orthographic>);
          return orthographic(p.height, aspect, p.near_plane, p.far_plane);
        }
      },
      projection);
}

} // namespace strafe::renderer
