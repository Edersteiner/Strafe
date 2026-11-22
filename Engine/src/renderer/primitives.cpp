#include "strafe/renderer/primitives.h"

#include "strafe/core/math.h"

#include <array>
#include <cmath>

namespace strafe::renderer {

MeshData cubeMeshData() {
  struct Face {
    Vec3 normal, u, v;
  };
  constexpr std::array<Face, 6> kFaces{{
      {.normal = {1, 0, 0}, .u = {0, 1, 0}, .v = {0, 0, 1}},
      {.normal = {-1, 0, 0}, .u = {0, 0, 1}, .v = {0, 1, 0}},
      {.normal = {0, 1, 0}, .u = {0, 0, 1}, .v = {1, 0, 0}},
      {.normal = {0, -1, 0}, .u = {1, 0, 0}, .v = {0, 0, 1}},
      {.normal = {0, 0, 1}, .u = {1, 0, 0}, .v = {0, 1, 0}},
      {.normal = {0, 0, -1}, .u = {0, 1, 0}, .v = {1, 0, 0}},
  }};
  constexpr std::array<Vec2, 4> kCorners{{{-1, -1}, {1, -1}, {1, 1}, {-1, 1}}};

  MeshData mesh;
  for (const Face &face : kFaces) {
    const auto first = static_cast<uint32_t>(mesh.vertices.size());
    for (const Vec2 corner : kCorners) {
      mesh.vertices.push_back(
          {.position = 0.5f * (face.normal + corner.x * face.u + corner.y * face.v),
           .uv = 0.5f * (corner + Vec2(1.0f)),
           .normal = face.normal});
    }
    for (const uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) {
      mesh.indices.push_back(first + i);
    }
  }
  return mesh;
}

MeshData sphereMeshData(uint32_t segments, uint32_t rings) {
  segments = std::max(segments, 3u);
  rings = std::max(rings, 2u);

  // The seam column is duplicated so its uvs can run from 0 to 1.
  MeshData mesh;
  for (uint32_t ring = 0; ring <= rings; ++ring) {
    const float v = static_cast<float>(ring) / static_cast<float>(rings);
    const float polar = v * kPi;
    for (uint32_t segment = 0; segment <= segments; ++segment) {
      const float u = static_cast<float>(segment) / static_cast<float>(segments);
      const float azimuth = u * 2.0f * kPi;
      const Vec3 normal(std::sin(polar) * std::cos(azimuth), std::cos(polar), -std::sin(polar) * std::sin(azimuth));
      mesh.vertices.push_back({.position = 0.5f * normal, .uv = {u, v}, .normal = normal});
    }
  }

  const uint32_t row = segments + 1;
  for (uint32_t ring = 0; ring < rings; ++ring) {
    for (uint32_t segment = 0; segment < segments; ++segment) {
      const uint32_t top = ring * row + segment;
      const uint32_t bottom = top + row;
      // Counter-clockwise from outside. The pole rows collapse one triangle
      // of each quad to a point, which draws nothing.
      for (const uint32_t i : {top, bottom, bottom + 1, top, bottom + 1, top + 1}) {
        mesh.indices.push_back(i);
      }
    }
  }
  return mesh;
}

MeshData planeMeshData() {
  MeshData mesh;
  constexpr Vec3 kUp(0.0f, 1.0f, 0.0f);
  constexpr std::array<Vec2, 4> kCorners{{{-0.5f, 0.5f}, {0.5f, 0.5f}, {0.5f, -0.5f}, {-0.5f, -0.5f}}};
  for (const Vec2 corner : kCorners) {
    mesh.vertices.push_back(
        {.position = {corner.x, 0.0f, corner.y}, .uv = {corner.x + 0.5f, corner.y + 0.5f}, .normal = kUp});
  }
  mesh.indices = {0, 1, 2, 0, 2, 3};
  return mesh;
}

MeshData primitiveMeshData(Primitive primitive) {
  switch (primitive) {
  case Primitive::Cube:
    return cubeMeshData();
  case Primitive::Sphere:
    return sphereMeshData();
  case Primitive::Plane:
  case Primitive::Count:
    break;
  }
  return planeMeshData();
}

} // namespace strafe::renderer
