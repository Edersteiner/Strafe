import strafe;

struct LitParams {
    float4 tint = float4(1.0);
    Texture albedo;
    Sampler sampler;
};

[shader("fragment"), Material("LitParams"), Blend("opaque")]
float4 fsMain(Surface surface) : SV_Target {
    let m = material<LitParams>();
    let albedo = m.albedo.sample(m.sampler, surface.uv) * m.tint;
    if (albedo.a == 0.0f) {
      discard;
    }
    return float4(lambert(surface, albedo.rgb), albedo.a);
}
