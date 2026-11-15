import strafe;

static const float4 wireframe_color = float4(0.0, 1.0, 1.0, 1.0);

[shader("fragment"), Polygon("line"), Cull("none")]
float4 fsMain(Surface surface) : SV_Target {
	return wireframe_color;
}
