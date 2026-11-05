#pragma once

#include "renderer/mesh.h"

#include <cstdint>
#include <vector>

namespace strafe::demo {

// Port of the course's generateSortofSphere: a unit sphere (radius 1) with a
// vertex at each pole and rings evenly spaced in height. Triangles are
// counter-clockwise from outside.
bool generateSphere(int rings, int slices,
                    std::vector<renderer::Vertex> &vertices,
                    std::vector<uint32_t> &indices);

} // namespace strafe::demo
