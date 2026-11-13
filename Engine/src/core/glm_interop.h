#pragma once

// Conversions between the engine's public math types and glm, which engine
// sources use internally.
//
#include "strafe/core/math.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace strafe {

static_assert(sizeof(Vec2) == sizeof(glm::vec2));
static_assert(sizeof(Vec3) == sizeof(glm::vec3));
static_assert(sizeof(Vec4) == sizeof(glm::vec4));
static_assert(sizeof(Mat4) == sizeof(glm::mat4));

inline glm::vec2 toGlm(Vec2 v) { return {v.x, v.y}; }
inline glm::vec3 toGlm(Vec3 v) { return {v.x, v.y, v.z}; }
inline glm::vec4 toGlm(Vec4 v) { return {v.x, v.y, v.z, v.w}; }

inline Vec2 fromGlm(glm::vec2 v) { return {v.x, v.y}; }
inline Vec3 fromGlm(glm::vec3 v) { return {v.x, v.y, v.z}; }
inline Vec4 fromGlm(glm::vec4 v) { return {v.x, v.y, v.z, v.w}; }

inline glm::quat toGlm(Quat q) {
  glm::quat result;
  result.x = q.x;
  result.y = q.y;
  result.z = q.z;
  result.w = q.w;
  return result;
}
inline Quat fromGlm(const glm::quat &q) { return {q.x, q.y, q.z, q.w}; }

inline glm::mat4 toGlm(const Mat4 &m) { return {toGlm(m[0]), toGlm(m[1]), toGlm(m[2]), toGlm(m[3])}; }
inline Mat4 fromGlm(const glm::mat4 &m) {
  Mat4 result;
  for (int column = 0; column < 4; ++column) {
    result[column] = fromGlm(m[column]);
  }
  return result;
}

} // namespace strafe
