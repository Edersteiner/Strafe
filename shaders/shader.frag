#version 460

#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec3 inColor;
layout(location = 1) in vec3 inWorldPos;
layout(location = 2) in vec3 inWorldNormal;
layout(location = 3) in vec2 inUV;

// Bindless set
layout(set = 0, binding = 0) uniform texture2D sampled_images[];
layout(set = 0, binding = 2) uniform sampler samplers[];

// Only the members this stage reads, at their offsets in PushConstants.
layout(push_constant) uniform PushConstants {
  layout(offset = 152) uint texture_index;
  layout(offset = 156) uint sampler_index;
} pc;

layout(location = 0) out vec4 fragColor;

// Hardcoded directional light.
const vec3 kLightDirection = normalize(vec3(-0.4, -1.0, -0.6));
const vec3 kLightColor = vec3(1.0, 0.95, 0.85);
// Stops faces turned away from the light from going pitch black.
const vec3 kAmbient = vec3(0.12, 0.13, 0.16);

// Colors each face by its world space normal instead of lighting it.
const bool kShowNormals = false;

void main() {
  vec3 normal = normalize(inWorldNormal);

  if (kShowNormals) {
    fragColor = vec4(normal * 0.5 + 0.5, 1.0);
    return;
  }

  float n_dot_l = max(dot(normal, -kLightDirection), 0.0);

  vec4 albedo = texture(
      sampler2D(sampled_images[nonuniformEXT(pc.texture_index)],
        samplers[nonuniformEXT(pc.sampler_index)]),
      inUV);
  vec3 lit = albedo.rgb * inColor * (kAmbient + kLightColor * n_dot_l);

  fragColor = vec4(lit, albedo.a);
}
