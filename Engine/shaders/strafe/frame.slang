// Data shared by every draw in a frame.
struct FrameData {
    float4x4 view_projection;
    float3 camera_position;
    float time;
    float3 light_direction;
    float3 light_color;
    float3 ambient;
};
