#version 460

layout(location = 0) in vec3 inColor;
layout(location = 1) in vec3 inWorldPos;
layout(location = 2) in vec3 inWorldNormal;

layout(location = 0) out vec4 fragColor;

// Colors each face by its world space normal instead of the draw color.
const bool kShowNormals = true;

void main() {
  fragColor = kShowNormals ? vec4(inWorldNormal, 1.0f) : vec4(inColor, 1.0);
}
