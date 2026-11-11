import strafe;

static const float3 kLightColor = float3(1.0, 0.95, 0.85);
static const float3 kAmbient = float3(0.12, 0.13, 0.16);

// Colors each face by its world space normal instead of lighting it.
static const bool kShowNormals = false;

[shader("vertex")]
Surface vsMain(uint vertex_id : SV_VulkanVertexID) {
    return standardVertex(vertex_id);
}

[shader("fragment"), Blend("opaque"), Cull("back")]
float4 fsMain(Surface surface) : SV_Target {
    let normal = normalize(surface.world_normal);
    if (kShowNormals) {
        return float4(normal * 0.5 + 0.5, 1.0);
    }

    let light_direction = normalize(float3(-0.4, -1.0, -0.6));
    let n_dot_l = max(dot(normal, -light_direction), 0.0);

    let albedo = g_draw.texture.sample(g_draw.sampler, surface.uv);
    let lit = albedo.rgb * surface.color * (kAmbient + kLightColor * n_dot_l);
    return float4(lit, albedo.a);
}
