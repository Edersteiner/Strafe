#version 460

layout(location = 0) in vec3 inColor;
layout(location = 1) in vec3 inWorldPos;
layout(location = 2) in vec3 inWorldNormal;

layout(location = 0) out vec4 fragColor;

// Hardcoded directional light.
const vec3 kLightDirection = normalize(vec3(-0.4, -1.0, -0.6));
const vec3 kLightColor = vec3(1.0, 0.95, 0.85);
// Stops faces turned away from the light from going pitch black.
const vec3 kAmbient = vec3(0.12, 0.13, 0.16);

// Colors each face by its world space normal instead of lighting it.
const bool kShowNormals = false;

void main() {
  // Interpolation shortens the normal between vertices, so renormalize.
  vec3 normal = normalize(inWorldNormal);

  if (kShowNormals) {
    fragColor = vec4(normal * 0.5 + 0.5, 1.0);
    return;
  }

  // Lambert: brightness is the cosine of the angle between the surface normal
  // and the direction towards the light, clamped so back faces get none.
  float n_dot_l = max(dot(normal, -kLightDirection), 0.0);
  vec3 lit = inColor * (kAmbient + kLightColor * n_dot_l);

  fragColor = vec4(lit, 1.0);
}
