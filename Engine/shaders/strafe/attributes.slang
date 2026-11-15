// Attributes the engine reads through reflection to build pipelines. They are
// all optional. Without one, the engine uses the documented default.

// Blend state of a fragment entry point: "opaque" (default), "alpha" or
// "additive".
[__AttributeUsage(_AttributeTargets.Function)]
struct BlendAttribute {
    string mode;
};

// Face culling: "back" (default), "front" or "none".
[__AttributeUsage(_AttributeTargets.Function)]
struct CullAttribute {
    string mode;
};

// Depth state: "test_write" (default), "test" or "off".
[__AttributeUsage(_AttributeTargets.Function)]
struct DepthAttribute {
    string mode;
};

// The render pass whose target formats the pipeline is built for:
// "forward" (default).
[__AttributeUsage(_AttributeTargets.Function)]
struct PassAttribute {
    string name;
};

// Names the struct holding this shader's material parameters, for example
// [Material("LitParams")].
[__AttributeUsage(_AttributeTargets.Function)]
struct MaterialAttribute {
    string type;
};
