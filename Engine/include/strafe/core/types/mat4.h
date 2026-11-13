#pragma once

#include "strafe/core/types/quat.h"
#include "strafe/core/types/vec3.h"
#include "strafe/core/types/vec4.h"

#include <array>

namespace strafe {

// Column-major, like GLSL and glm.
struct Mat4 {
  std::array<Vec4, 4> columns{
      Vec4(1.0f, 0.0f, 0.0f, 0.0f), Vec4(0.0f, 1.0f, 0.0f, 0.0f), Vec4(0.0f, 0.0f, 1.0f, 0.0f),
      Vec4(0.0f, 0.0f, 0.0f, 1.0f)};

  constexpr Vec4 &operator[](int column) { return columns[column]; }
  constexpr const Vec4 &operator[](int column) const { return columns[column]; }

  bool operator==(const Mat4 &) const = default;
};

constexpr Vec4 operator*(const Mat4 &m, Vec4 v) { return m[0] * v.x + m[1] * v.y + m[2] * v.z + m[3] * v.w; }

// Applies b first, then a.
constexpr Mat4 operator*(const Mat4 &a, const Mat4 &b) {
  Mat4 result;
  for (int column = 0; column < 4; ++column) {
    result[column] = a * b[column];
  }
  return result;
}

Mat4 translation(Vec3 offset);
Mat4 rotation(Quat rotation);
Mat4 scaling(Vec3 scale);

Mat4 lookAt(Vec3 eye, Vec3 target, Vec3 up);

Mat4 inverse(const Mat4 &m);
Mat4 transpose(const Mat4 &m);

} // namespace strafe
