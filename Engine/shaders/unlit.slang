import strafe;

// Texture times tint, ignoring the light.
struct UnlitParams {
    float4 tint = float4(1.0);
    Texture albedo;
    Sampler sampler;
};

[shader("fragment"), Material("UnlitParams")]
float4 fsMain(Surface surface) : SV_Target {
    let m = material<UnlitParams>();
    return m.albedo.sample(m.sampler, surface.uv) * m.tint;
}
