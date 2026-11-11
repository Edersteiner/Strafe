// Per-draw data and vertex fetching.
import strafe.bindless;

// Must match renderer::Vertex byte for byte (scalar layout, 32 bytes).
struct Vertex {
    float3 position;
    float2 uv;
    float3 normal;
};

// The one push constant block every shader shares. Must match PushConstants in renderer.cpp.
struct DrawPush {
    float4x4 transform;
    float4x4 view_projection;
    Vertex* vertices;
    float4 color;
    Texture texture;
    Sampler sampler;
};

[[vk::push_constant]] ConstantBuffer<DrawPush, ScalarDataLayout> g_draw;

// Vertices are pulled through the mesh's buffer address, so there is no
// vertex input state. The id is the value read from the index buffer.
Vertex fetchVertex(uint vertex_id) {
    return g_draw.vertices[vertex_id];
}
