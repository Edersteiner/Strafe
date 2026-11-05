#pragma once

#include "renderer/frame_desc.h"
#include "renderer/mesh.h"

namespace strafe::renderer {
class Renderer;
} // namespace strafe::renderer

namespace strafe::demo {

struct House {
  renderer::MeshId triangle;
  int roof_color = 0;
};

// Uploads the single triangle every shape of the house is drawn from.
bool createHouseMesh(renderer::Renderer &renderer, House &house);

// Rotation is in radians.
void drawTriangle(renderer::FrameDesc &frame, renderer::MeshId triangle,
                  glm::vec2 position, glm::vec2 scale, float rotation,
                  glm::vec4 color);

void drawRectangle(renderer::FrameDesc &frame, renderer::MeshId triangle,
                   glm::vec2 position, glm::vec2 size, glm::vec4 color);

void cycleRoofColor(House &house);
void buildHouse(renderer::FrameDesc &frame, const House &house);

} // namespace strafe::demo
