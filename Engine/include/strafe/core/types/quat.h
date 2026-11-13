#pragma once

#include "strafe/core/types/vec3.h"

namespace strafe {

// A rotation. The default is no rotation.
struct Quat {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  float w = 1.0f;

  constexpr Quat() = default;
  constexpr Quat(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}

  // Counter-clockwise around the axis when it points towards the viewer.
  // Similair to glm::Rotate.
  static Quat fromAxisAngle(Vec3 axis, float radians);

  static Quat fromEuler(Vec3 radians);

  bool operator==(const Quat &) const = default;
};

// Applies b first, then a.
Quat operator*(Quat a, Quat b);
// Rotates v.
Vec3 operator*(Quat q, Vec3 v);

Quat normalize(Quat q);
Quat inverse(Quat q);
// Takes the shortest path between a and b.
Quat slerp(Quat a, Quat b, float t);

} // namespace strafe
