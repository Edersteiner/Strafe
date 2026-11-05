#include "demo/week1_house.h"

#include "renderer/renderer.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <optional>

#include <glm/gtc/matrix_transform.hpp>

namespace strafe::demo {

namespace {
// A right triangle covering half of a unit square centered on the origin.
constexpr std::array kTriangleVertices{
    renderer::Vertex{.position = {-0.5f, -0.5f, 0.0f}, .color = {1, 1, 1}},
    renderer::Vertex{.position = {-0.5f, 0.5f, 0.0f}, .color = {1, 1, 1}},
    renderer::Vertex{.position = {0.5f, 0.5f, 0.0f}, .color = {1, 1, 1}},
};
constexpr std::array<uint32_t, 3> kTriangleIndices{0, 1, 2};

constexpr std::array kRoofColors{
    glm::vec4(0.8f, 0.1f, 0.1f, 1.0f),
    glm::vec4(0.1f, 0.8f, 0.1f, 1.0f),
    glm::vec4(0.1f, 0.1f, 0.8f, 1.0f),
};

constexpr glm::vec4 kWallColor(0.15f, 0.15f, 0.15f, 1.0f);
constexpr glm::vec4 kWindowColor(0.2f, 0.6f, 0.9f, 1.0f);
constexpr glm::vec4 kDoorColor(0.45f, 0.3f, 0.15f, 1.0f);

// Every other part of the house is placed relative to the body.
constexpr glm::vec2 kBodyPosition(0.0f, 0.3f);
constexpr glm::vec2 kBodySize(0.8f, 0.8f);
constexpr float kRoofHeight = 0.5f;
} // namespace

bool createHouseMesh(renderer::Renderer &renderer, House &house) {
  const std::optional<renderer::MeshId> triangle =
      renderer.createMesh(kTriangleVertices, kTriangleIndices);
  if (!triangle) {
    return false;
  }
  house.triangle = *triangle;
  return true;
}

void drawTriangle(renderer::FrameDesc &frame, renderer::MeshId triangle,
                  glm::vec2 position, glm::vec2 scale, float rotation,
                  glm::vec4 color) {
  glm::mat4 transform(1.0f);
  transform = glm::translate(transform, glm::vec3(position, 0.0f));
  transform = glm::rotate(transform, rotation, glm::vec3(0.0f, 0.0f, 1.0f));
  transform = glm::scale(transform, glm::vec3(scale, 1.0f));

  frame.draws.push_back(
      {.mesh = triangle, .transform = transform, .color = color});
}

void drawRectangle(renderer::FrameDesc &frame, renderer::MeshId triangle,
                   glm::vec2 position, glm::vec2 size, glm::vec4 color) {
  drawTriangle(frame, triangle, position, size, 0.0f, color);
  drawTriangle(frame, triangle, position, size, std::numbers::pi_v<float>,
               color);
}

void cycleRoofColor(House &house) {
  house.roof_color =
      (house.roof_color + 1) % static_cast<int>(kRoofColors.size());
}

void buildHouse(renderer::FrameDesc &frame, const House &house) {
  // Body
  const float body_top = kBodyPosition.y - kBodySize.y / 2.0f;
  const float body_bottom = kBodyPosition.y + kBodySize.y / 2.0f;

  drawRectangle(frame, house.triangle, kBodyPosition, kBodySize, kWallColor);

  // Window
  const glm::vec2 window_size = kBodySize * 0.2f;
  drawRectangle(frame, house.triangle,
                kBodyPosition + kBodySize * glm::vec2(-0.25f, -0.2f),
                window_size, kWindowColor);
  drawRectangle(frame, house.triangle,
                kBodyPosition + kBodySize * glm::vec2(0.25f, -0.2f),
                window_size, kWindowColor);

  // Door
  const glm::vec2 door_size = kBodySize * glm::vec2(0.2f, 0.4f);
  drawRectangle(frame, house.triangle,
                {kBodyPosition.x, body_bottom - door_size.y / 2.0f}, door_size,
                kDoorColor);

  // Roof
  const glm::vec4 roof_color =
      kRoofColors[static_cast<std::size_t>(house.roof_color) %
                  kRoofColors.size()];
  drawTriangle(frame, house.triangle,
               {kBodyPosition.x, body_top - kRoofHeight / 2.0f},
               {kBodySize.x * 1.25f, kRoofHeight}, 0.0f, roof_color);
}

} // namespace strafe::demo
