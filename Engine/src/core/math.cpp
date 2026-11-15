#include "strafe/core/math.h"

#include "core/glm_interop.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace strafe {

Quat Quat::fromAxisAngle(Vec3 axis, float radians) {
  // A zero axis has no direction to turn around, and would normalize to NaN.
  if (dot(axis, axis) == 0.0f) {
    return {};
  }
  return fromGlm(glm::angleAxis(radians, glm::normalize(toGlm(axis))));
}

Quat Quat::fromEuler(Vec3 radians) {
  return fromAxisAngle({0.0f, 1.0f, 0.0f}, radians.y) * fromAxisAngle({1.0f, 0.0f, 0.0f}, radians.x) *
      fromAxisAngle({0.0f, 0.0f, 1.0f}, radians.z);
}

Quat operator*(Quat a, Quat b) { return fromGlm(toGlm(a) * toGlm(b)); }
Vec3 operator*(Quat q, Vec3 v) { return fromGlm(toGlm(q) * toGlm(v)); }

Quat normalize(Quat q) { return fromGlm(glm::normalize(toGlm(q))); }
Quat inverse(Quat q) { return fromGlm(glm::inverse(toGlm(q))); }
Quat slerp(Quat a, Quat b, float t) { return fromGlm(glm::slerp(toGlm(a), toGlm(b), t)); }

Mat4 translation(Vec3 offset) { return fromGlm(glm::translate(glm::mat4(1.0f), toGlm(offset))); }
Mat4 rotation(Quat rotation) { return fromGlm(glm::mat4_cast(toGlm(rotation))); }
Mat4 scaling(Vec3 scale) { return fromGlm(glm::scale(glm::mat4(1.0f), toGlm(scale))); }

Mat4 lookAt(Vec3 eye, Vec3 target, Vec3 up) { return fromGlm(glm::lookAtRH(toGlm(eye), toGlm(target), toGlm(up))); }

Mat4 inverse(const Mat4 &m) { return fromGlm(glm::inverse(toGlm(m))); }
Mat4 transpose(const Mat4 &m) { return fromGlm(glm::transpose(toGlm(m))); }

Mat4 Transform::toMatrix() const {
  return fromGlm(
      glm::translate(glm::mat4(1.0f), toGlm(position)) * glm::mat4_cast(toGlm(rotation)) *
      glm::scale(glm::mat4(1.0f), toGlm(scale)));
}

} // namespace strafe
