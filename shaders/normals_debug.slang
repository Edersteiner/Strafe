import strafe;

// Colors each pixel by its world space normal.
[shader("fragment")]
float4 fsMain(Surface surface) : SV_Target {
    return float4(normalize(surface.world_normal) * 0.5 + 0.5, 1.0);
}
