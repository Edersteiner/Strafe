#version 460

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

struct Vertex {
  vec3 position;
  vec2 uv;
  vec3 normal;
};

layout(buffer_reference, scalar) readonly buffer VertexBuffer {
  Vertex vertices[];
};

layout(push_constant, scalar) uniform PushConstants {
  mat4 transform;
  mat4 view_projection;
  VertexBuffer vertex_buffer;
  vec4 color;
} pc;

layout(location = 0) out vec3 outColor;
layout(location = 1) out vec3 outWorldPos;
layout(location = 2) out vec3 outWorldNormal;

void main() {
  Vertex vertex = pc.vertex_buffer.vertices[gl_VertexIndex];
  vec4 world_pos = pc.transform * vec4(vertex.position, 1.0);
  gl_Position = pc.view_projection * world_pos;
  outColor = pc.color.rgb;
  outWorldPos = world_pos.xyz;
  outWorldNormal = normalize(mat3(pc.transform) * vertex.normal);
}
