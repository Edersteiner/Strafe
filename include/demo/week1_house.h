#pragma once

#include "renderer/frame_desc.h"

namespace strafe::demo {

struct House {
  int roof_color = 0;
};

void drawTriangle(renderer::FrameDesc &frame, glm::vec2 position,
                  glm::vec2 scale, float rotation, glm::vec4 color);

void drawRectangle(renderer::FrameDesc &frame, glm::vec2 position,
                   glm::vec2 size, glm::vec4 color);

void cycleRoofColor(House &house);
void buildHouse(renderer::FrameDesc &frame, const House &house);

} // namespace strafe::demo
