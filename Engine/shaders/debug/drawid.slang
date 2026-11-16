import strafe;

uint pcg_hash(uint v)
{
    uint state = v * 747796405u + 2891336453u;
    uint word  = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

float3 hsv_to_rgb(float3 c)
{
    float3 p = abs(frac(c.xxx + float3(0.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0);
    return c.z * lerp(1.0.xxx, saturate(p - 1.0), c.y);
}

float3 id_to_color(uint id)
{
    if (id == -1) float3(0, 0, 0); // treat -1 as "no object"

    uint h = pcg_hash(id);

    float hue = float(h & 0xFFFFu) / 65535.0;
    float sat = 0.65 + 0.35 * float((h >> 16) & 0xFFu) / 255.0;
    float val = 0.75 + 0.25 * float((h >> 24) & 0xFFu) / 255.0;

    return hsv_to_rgb(float3(hue, sat, val));
}

// Colors each object based on it's draw ID, which is unique for each draw call.
[shader("fragment")]
float4 fsMain(Surface surface) : SV_Target {
    return float4(id_to_color(drawId()), 1.0);
}
