// What the standard vertex shader hands to fragment shaders.
import strafe.draw;

struct Surface {
    float4 position : SV_Position;
    float3 world_position : WORLD_POSITION;
    float3 world_normal : WORLD_NORMAL;
    float2 uv : TEXCOORD0;
    float3 color : COLOR0;
};

// Transforms the current vertex. Every shader without its own vertex logic
// uses this.
// Todo: mat3(transform) is only correct for uniform scale. Switch to a normal
// matrix once non-uniform scale shows up.
Surface standardVertex(uint vertex_id) {
    let vertex = fetchVertex(vertex_id);
    let world_position = mul(g_draw.transform, float4(vertex.position, 1.0));

    Surface surface;
    surface.position = mul(g_draw.view_projection, world_position);
    surface.world_position = world_position.xyz;
    surface.world_normal = mul((float3x3)g_draw.transform, vertex.normal);
    surface.uv = vertex.uv;
    surface.color = g_draw.color.rgb;
    return surface;
}
