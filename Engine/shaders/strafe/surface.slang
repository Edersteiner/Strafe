// What the standard vertex shader hands to fragment shaders.
import strafe.draw;

struct Surface {
    float4 position : SV_Position;
    float3 world_position : WORLD_POSITION;
    float3 world_normal : WORLD_NORMAL;
    float2 uv : TEXCOORD0;
};

// The cofactor matrix transforms normals correctly under non-uniform scale.
// It is the inverse transpose times the determinant, so it needs no inverse,
// and the extra scale goes away when the normal is normalized.
float3 transformNormal(float3x3 m, float3 normal) {
    // Rows of the transpose are the columns of m.
    let c = transpose(m);
    return cross(c[1], c[2]) * normal.x + cross(c[2], c[0]) * normal.y + cross(c[0], c[1]) * normal.z;
}

// Transforms the current vertex. Every shader without its own vertex logic
// uses this.
Surface standardVertex(uint vertex_id) {
    let vertex = fetchVertex(vertex_id);
    let world_position = mul(g_draw.transform, float4(vertex.position, 1.0));

    Surface surface;
    surface.position = mul(frame().view_projection, world_position);
    surface.world_position = world_position.xyz;
    surface.world_normal = transformNormal((float3x3)g_draw.transform, vertex.normal);
    surface.uv = vertex.uv;
    return surface;
}
