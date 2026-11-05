#version 460

layout(location = 0) in vec3 inColor;
layout(location = 1) in vec3 inWorldPos;
layout(location = 0) out vec4 fragColor;

// Colors each face by its world space normal instead of the draw color.
const bool kShowNormals = true;

void main() {
  // Both screen space derivatives lie in the triangle, so their cross product
  // is its face normal. Screen Y points down, which makes dFdy x dFdx point
  // out of the surface.
  vec3 normal = normalize(cross(dFdy(inWorldPos), dFdx(inWorldPos)));
  fragColor = kShowNormals ? vec4(normal * 0.5 + 0.5, 1.0)
                           : vec4(inColor, 1.0);
}
