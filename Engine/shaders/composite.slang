import strafe;

struct CompositeParams {
	Texture source;
	Sampler sampler;
}

struct FullscreenVertex {
	float4 position : SV_Position;
	float2 uv : TEXCOORD0;
}

[shader("vertex")]
FullscreenVertex vsMain(uint vertex_id : SV_VulkanVertexID) {
	let uv = float2(float((vertex_id << 1) & 2), float(vertex_id & 2));
	FullscreenVertex output;
	output.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
	output.uv = uv;
	return output;
}

[shader("fragment"), Material("CompositeParams"), Pass("composite"), Cull("none"), Depth("off")]
float4 fsMain(FullscreenVertex vertex) : SV_Target {
	let m = material<CompositeParams>();
	let color = m.source.sample(m.sampler, vertex.uv);
	// The swapchain is UNORM, so the sRGB encode happens here.
	return float4(linearToSrgb(saturate(color.rgb)), color.a);
}
