#version 460

#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

struct Vertex {
  vec3 position;
  vec3 color;
};

layout(buffer_reference, scalar) readonly buffer VertexBuffer {
  Vertex vertices[];
};

layout(push_constant, scalar) uniform PushConstants {
  mat4 transform;
  VertexBuffer vertex_buffer;
  vec4 color;
} pc;

layout(location = 0) out vec3 outColor;

void main() {
  Vertex vertex = pc.vertex_buffer.vertices[gl_VertexIndex];
  gl_Position = pc.transform * vec4(vertex.position, 1.0);
  outColor = vertex.color * pc.color.rgb;
}
