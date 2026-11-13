#pragma once

#include "strafe/core/types/vec3.h"

#include <cmath>

namespace strafe {

struct Vec4 {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  float w = 0.0f;

  constexpr Vec4() = default;
  constexpr Vec4(float x, float y, float z, float w) : x(x), y(y), z(z), w(w) {}
  constexpr Vec4(Vec3 v, float w) : x(v.x), y(v.y), z(v.z), w(w) {}
  // Sets every component to the same value.
  constexpr explicit Vec4(float s) : x(s), y(s), z(s), w(s) {}

  constexpr Vec3 xyz() const { return {x, y, z}; }

  bool operator==(const Vec4 &) const = default;
};

constexpr Vec4 operator+(Vec4 a, Vec4 b) { return {a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w}; }
constexpr Vec4 operator-(Vec4 a, Vec4 b) { return {a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w}; }
constexpr Vec4 operator*(Vec4 a, Vec4 b) { return {a.x * b.x, a.y * b.y, a.z * b.z, a.w * b.w}; }
constexpr Vec4 operator/(Vec4 a, Vec4 b) { return {a.x / b.x, a.y / b.y, a.z / b.z, a.w / b.w}; }
constexpr Vec4 operator*(Vec4 v, float s) { return {v.x * s, v.y * s, v.z * s, v.w * s}; }
constexpr Vec4 operator*(float s, Vec4 v) { return v * s; }
constexpr Vec4 operator/(Vec4 v, float s) { return {v.x / s, v.y / s, v.z / s, v.w / s}; }
constexpr Vec4 operator-(Vec4 v) { return {-v.x, -v.y, -v.z, -v.w}; }

constexpr Vec4 &operator+=(Vec4 &a, Vec4 b) { return a = a + b; }
constexpr Vec4 &operator-=(Vec4 &a, Vec4 b) { return a = a - b; }
constexpr Vec4 &operator*=(Vec4 &v, float s) { return v = v * s; }
constexpr Vec4 &operator/=(Vec4 &v, float s) { return v = v / s; }

constexpr float dot(Vec4 a, Vec4 b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
inline float length(Vec4 v) { return std::sqrt(dot(v, v)); }

// A zero vector stays zero rather than becoming NaN.
inline Vec4 normalize(Vec4 v) {
  const float len = length(v);
  return len > 0.0f ? v / len : Vec4{};
}

constexpr Vec4 lerp(Vec4 a, Vec4 b, float t) { return a + (b - a) * t; }

} // namespace strafe
