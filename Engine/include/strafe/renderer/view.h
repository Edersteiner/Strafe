#pragma once

#include "strafe/core/types/vec4.h"
#include "strafe/renderer/camera.h"

#include <cstdint>

namespace strafe::renderer {

struct ViewId {
  uint32_t value = 0;

  bool operator==(const ViewId &) const = default;
};

// The id of the view the renderer adds when a frame has none.
inline constexpr ViewId kDefaultViewId{UINT32_MAX};

// A rectangle on the window in normalized coordinates.
struct ScreenRegion {
  float x = 0.0f;
  float y = 0.0f;
  float width = 1.0f;
  float height = 1.0f;

  static constexpr ScreenRegion full() { return {}; }
  static constexpr ScreenRegion left() { return {.width = 0.5f}; }
  static constexpr ScreenRegion right() { return {.x = 0.5f, .width = 0.5f}; }
  static constexpr ScreenRegion top() { return {.height = 0.5f}; }
  static constexpr ScreenRegion bottom() { return {.y = 0.5f, .height = 0.5f}; }
};

// One camera rendered into a region of the window. Every view sees every draw
// in the frame.
struct ViewDesc {
  ViewId id;
  Camera camera;
  ScreenRegion region;
  Vec4 clear_color{0.01f, 0.01f, 0.01f, 1.0f};
  // Renders at this fraction of the region's pixel size. The composite scales
  // the result to fill the region.
  float resolution_scale = 1.0f;
};

} // namespace strafe::renderer
