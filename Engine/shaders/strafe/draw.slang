// Per-draw data and vertex fetching.
__exported import strafe.frame;

// Must match renderer::Vertex byte for byte (scalar layout, 32 bytes).
struct Vertex {
    float3 position;
    float2 uv;
    float3 normal;
};

// The one push constant block every shader shares.
struct DrawPush {
    float4x4 transform;
    FrameData* frame;
    void* material; // The draw's material block
    Vertex* vertices;
};

[[vk::push_constant]] ConstantBuffer<DrawPush, ScalarDataLayout> g_draw;

// The current frame's camera, light and time.
FrameData frame() {
    return *g_draw.frame;
}

// The draw's material parameters. T must be the struct named by the entry
// point's [Material("...")], which is the layout the engine packs.
T material<T>() {
    return *(T*)g_draw.material;
}

// Vertices are pulled through the mesh's buffer address, so there is no
// vertex input state. The id is the value read from the index buffer.
Vertex fetchVertex(uint vertex_id) {
    return g_draw.vertices[vertex_id];
}
