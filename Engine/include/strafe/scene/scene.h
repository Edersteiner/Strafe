#pragma once

#include "strafe/core/math.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace strafe::scene {

// An axis aligned box.
struct [[reflect]] Brush {
  Vec3 min;
  Vec3 max;
  Vec4 color{1.0f};

  Transform transform() const { return {.position = 0.5f * (min + max), .scale = max - min}; }
};

// A named transform for game code to use, such as a spawn point.
struct [[reflect]] Marker {
  std::string name;
  Transform transform;
};

struct [[reflect]] Object {
  std::string name;
  std::string model_path;
  Transform transform;
  Vec4 color{1.0f};
};

struct Scene {
  std::string name;
  std::vector<Brush> brushes;
  std::vector<Marker> markers;
  std::vector<Object> objects;

  // The first marker with this name, or null.
  const Marker *findMarker(std::string_view marker_name) const;
  // Every marker with this name, in file order.
  std::vector<const Marker *> findMarkers(std::string_view marker_name) const;
};

// Reads a .scene file.
std::optional<Scene> loadScene(std::string_view path);

} // namespace strafe::scene

#include "strafe/scene/scene.gen.h"
