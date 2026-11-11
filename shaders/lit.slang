import strafe;

// Colors each face by its world space normal instead of lighting it.
static const bool kShowNormals = false;

struct LitParams {
    float4 tint;
    Texture albedo;
    Sampler sampler;
};

[shader("vertex")]
Surface vsMain(uint vertex_id : SV_VulkanVertexID) {
    return standardVertex(vertex_id);
}

[shader("fragment"), Material("LitParams"), Blend("alpha"), Cull("back")]
float4 fsMain(Surface surface) : SV_Target {
    let normal = normalize(surface.world_normal);
    if (kShowNormals) {
        return float4(normal * 0.5 + 0.5, 1.0);
    }

    let f = frame();
    let n_dot_l = max(dot(normal, -normalize(f.light_direction)), 0.0);

    let m = material<LitParams>();
    let albedo = m.albedo.sample(m.sampler, surface.uv) * m.tint;
    let lit = albedo.rgb * (f.ambient + f.light_color * n_dot_l);
    return float4(lit, albedo.a);
}
