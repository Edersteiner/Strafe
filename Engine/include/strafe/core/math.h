#pragma once

// Umbrella header for the engine's public math types and their associated functions.

// IWYU pragma: begin_exports
#include "strafe/core/types/mat4.h"
#include "strafe/core/types/quat.h"
#include "strafe/core/types/transform.h"
#include "strafe/core/types/vec2.h"
#include "strafe/core/types/vec3.h"
#include "strafe/core/types/vec4.h"
// IWYU pragma: end_exports

#include <numbers>

namespace strafe {

inline constexpr float kPi = std::numbers::pi_v<float>;

constexpr float radians(float degrees) { return degrees * (kPi / 180.0f); }
constexpr float degrees(float radians) { return radians * (180.0f / kPi); }

} // namespace strafe
