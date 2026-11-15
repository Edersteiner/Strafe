#pragma once

#include <cmath>

namespace strafe {

struct Vec3 {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;

  constexpr Vec3() = default;
  constexpr Vec3(float x, float y, float z) : x(x), y(y), z(z) {}
  // Sets every component to the same value.
  constexpr explicit Vec3(float s) : x(s), y(s), z(s) {}

  bool operator==(const Vec3 &) const = default;
};

constexpr Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
constexpr Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
constexpr Vec3 operator*(Vec3 a, Vec3 b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
constexpr Vec3 operator/(Vec3 a, Vec3 b) { return {a.x / b.x, a.y / b.y, a.z / b.z}; }
constexpr Vec3 operator*(Vec3 v, float s) { return {v.x * s, v.y * s, v.z * s}; }
constexpr Vec3 operator*(float s, Vec3 v) { return v * s; }
constexpr Vec3 operator/(Vec3 v, float s) { return {v.x / s, v.y / s, v.z / s}; }
constexpr Vec3 operator-(Vec3 v) { return {-v.x, -v.y, -v.z}; }

constexpr Vec3 &operator+=(Vec3 &a, Vec3 b) { return a = a + b; }
constexpr Vec3 &operator-=(Vec3 &a, Vec3 b) { return a = a - b; }
constexpr Vec3 &operator*=(Vec3 &v, float s) { return v = v * s; }
constexpr Vec3 &operator/=(Vec3 &v, float s) { return v = v / s; }

constexpr float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

// Right-handed: cross(+X, +Y) is +Z.
constexpr Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }

inline float length(Vec3 v) { return std::sqrt(dot(v, v)); }

// A zero vector stays zero rather than becoming NaN.
inline Vec3 normalize(Vec3 v) {
  const float len = length(v);
  return len > 0.0f ? v / len : Vec3{};
}

constexpr Vec3 lerp(Vec3 a, Vec3 b, float t) { return a + (b - a) * t; }

} // namespace strafe
