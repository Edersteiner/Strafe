#pragma once

#include <cmath>

namespace strafe {

struct Vec2 {
  float x = 0.0f;
  float y = 0.0f;

  constexpr Vec2() = default;
  constexpr Vec2(float x, float y) : x(x), y(y) {}
  // Sets every component to the same value.
  constexpr explicit Vec2(float s) : x(s), y(s) {}

  bool operator==(const Vec2 &) const = default;
};

constexpr Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
constexpr Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
constexpr Vec2 operator*(Vec2 a, Vec2 b) { return {a.x * b.x, a.y * b.y}; }
constexpr Vec2 operator/(Vec2 a, Vec2 b) { return {a.x / b.x, a.y / b.y}; }
constexpr Vec2 operator*(Vec2 v, float s) { return {v.x * s, v.y * s}; }
constexpr Vec2 operator*(float s, Vec2 v) { return v * s; }
constexpr Vec2 operator/(Vec2 v, float s) { return {v.x / s, v.y / s}; }
constexpr Vec2 operator-(Vec2 v) { return {-v.x, -v.y}; }

constexpr Vec2 &operator+=(Vec2 &a, Vec2 b) { return a = a + b; }
constexpr Vec2 &operator-=(Vec2 &a, Vec2 b) { return a = a - b; }
constexpr Vec2 &operator*=(Vec2 &v, float s) { return v = v * s; }
constexpr Vec2 &operator/=(Vec2 &v, float s) { return v = v / s; }

constexpr float dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
inline float length(Vec2 v) { return std::sqrt(dot(v, v)); }

// A zero vector stays zero rather than becoming NaN.
inline Vec2 normalize(Vec2 v) {
  const float len = length(v);
  return len > 0.0f ? v / len : Vec2{};
}

constexpr Vec2 lerp(Vec2 a, Vec2 b, float t) { return a + (b - a) * t; }

} // namespace strafe
