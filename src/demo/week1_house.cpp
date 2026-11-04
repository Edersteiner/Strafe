#include "demo/week1_house.h"

#include <array>
#include <cstddef>
#include <numbers>

#include <glm/gtc/matrix_transform.hpp>

namespace strafe::demo {

namespace {
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

void drawTriangle(renderer::FrameDesc &frame, glm::vec2 position,
                  glm::vec2 scale, float rotation, glm::vec4 color) {
  glm::mat4 transform(1.0f);
  transform = glm::translate(transform, glm::vec3(position, 0.0f));
  transform = glm::rotate(transform, rotation, glm::vec3(0.0f, 0.0f, 1.0f));
  transform = glm::scale(transform, glm::vec3(scale, 1.0f));

  frame.draws.push_back({.transform = transform, .color = color});
}

void drawRectangle(renderer::FrameDesc &frame, glm::vec2 position,
                   glm::vec2 size, glm::vec4 color) {
  drawTriangle(frame, position, size, 0.0f, color);
  drawTriangle(frame, position, size, std::numbers::pi_v<float>, color);
}

void cycleRoofColor(House &house) {
  house.roof_color =
      (house.roof_color + 1) % static_cast<int>(kRoofColors.size());
}

void buildHouse(renderer::FrameDesc &frame, const House &house) {
  // Body
  const float body_top = kBodyPosition.y - kBodySize.y / 2.0f;
  const float body_bottom = kBodyPosition.y + kBodySize.y / 2.0f;

  drawRectangle(frame, kBodyPosition, kBodySize, kWallColor);

  // Window
  const glm::vec2 window_size = kBodySize * 0.2f;
  drawRectangle(frame, kBodyPosition + kBodySize * glm::vec2(-0.25f, -0.2f),
                window_size, kWindowColor);
  drawRectangle(frame, kBodyPosition + kBodySize * glm::vec2(0.25f, -0.2f),
                window_size, kWindowColor);

  // Door
  const glm::vec2 door_size = kBodySize * glm::vec2(0.2f, 0.4f);
  drawRectangle(frame, {kBodyPosition.x, body_bottom - door_size.y / 2.0f},
                door_size, kDoorColor);

  // Roof
  const glm::vec4 roof_color =
      kRoofColors[static_cast<std::size_t>(house.roof_color) %
                  kRoofColors.size()];
  drawTriangle(frame, {kBodyPosition.x, body_top - kRoofHeight / 2.0f},
               {kBodySize.x * 1.25f, kRoofHeight}, 0.0f, roof_color);
}

} // namespace strafe::demo
