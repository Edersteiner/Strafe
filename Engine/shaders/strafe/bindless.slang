// Mirrors gfx::BindlessSet: one global descriptor set, bound once per command
// buffer. Shaders reach images and samplers through the handle types below and
// never index the arrays themselves. Binding 1 holds storage images, which have
// no users yet.

[[vk::binding(0, 0)]] Texture2D g_sampled_images[];
[[vk::binding(2, 0)]] SamplerState g_samplers[];

// A gfx::Sampler preset.
struct Sampler {
    uint index;
};

// A slot in the sampled image array.
struct Texture {
    uint index;

    float4 sample(Sampler sampler, float2 uv) {
        // NonUniformResourceIndex keeps this correct once the index varies
        // within a draw, for example when it comes from per-instance data.
        return g_sampled_images[NonUniformResourceIndex(index)].Sample(
            g_samplers[NonUniformResourceIndex(sampler.index)], uv);
    }
};
