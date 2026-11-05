#include "demo/sphere.h"

#include "core/log.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace strafe::demo {

bool generateSphere(int rings, int slices,
                    std::vector<renderer::Vertex> &vertices,
                    std::vector<uint32_t> &indices) {
  if (rings < 3) {
    log::error("No sphere generated, rings must be at least 3");
    return false;
  }
  if (slices < 4) {
    log::error("No sphere generated, slices must be at least 4");
    return false;
  }

  vertices.clear();
  indices.clear();

  const float angle_step =
      2.0f * std::numbers::pi_v<float> / static_cast<float>(slices);
  const float y_step = 2.0f / static_cast<float>(rings - 1);

  // Vertex 0 is the top pole, then each middle ring adds `slices` vertices,
  // and the last vertex is the bottom pole.
  for (int ring = 0; ring < rings; ++ring) {
    // Computed from the ring number rather than accumulated, so the last ring
    // lands exactly on -1. A value just past -1 would make the radius NaN.
    const float y = 1.0f - static_cast<float>(ring) * y_step;
    const float radius = std::sqrt(std::max(0.0f, 1.0f - y * y));

    const bool pole = ring == 0 || ring == rings - 1;
    const int ring_slices = pole ? 1 : slices;
    for (int slice = 0; slice < ring_slices; ++slice) {
      const float angle = static_cast<float>(slice) * angle_step;
      vertices.push_back(
          {.position = {std::cos(angle) * radius, y, std::sin(angle) * radius},
           .color = {1.0f, 1.0f, 1.0f}});
    }
  }

  const auto top = 0u;
  const auto bottom = static_cast<uint32_t>(vertices.size() - 1);
  const auto ring_count = static_cast<uint32_t>(rings);
  const auto slice_count = static_cast<uint32_t>(slices);

  // First vertex of middle ring r (1 to rings - 2).
  auto ring_start = [slice_count](uint32_t r) {
    return 1 + (r - 1) * slice_count;
  };

  // Band i connects ring i to ring i + 1. The first and last bands are fans
  // around the poles, the rest are quads split into two triangles. The slice
  // angle turns from +X towards +Z, which is clockwise seen from above, so each
  // triangle lists its corners in the reverse of the course code's order to
  // stay counter-clockwise from outside.
  for (uint32_t i = 0; i < ring_count - 1; ++i) {
    for (uint32_t j = 0; j < slice_count; ++j) {
      // The last slice wraps around to slice 0 to close the seam.
      const uint32_t next = (j + 1) % slice_count;

      if (i == 0) {
        indices.insert(indices.end(),
                       {top, ring_start(1) + next, ring_start(1) + j});
      } else if (i == ring_count - 2) {
        indices.insert(indices.end(),
                       {ring_start(i) + j, ring_start(i) + next, bottom});
      } else {
        const uint32_t tl = ring_start(i) + j;
        const uint32_t tr = ring_start(i) + next;
        const uint32_t bl = ring_start(i + 1) + j;
        const uint32_t br = ring_start(i + 1) + next;
        indices.insert(indices.end(), {tl, br, bl, tl, tr, br});
      }
    }
  }

  log::debug("Sphere: {} vertices, {} triangles", vertices.size(),
             indices.size() / 3);
  return true;
}

} // namespace strafe::demo
