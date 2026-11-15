// The vertex shader a shader gets when it doesn't define one. The engine links
// this entry point in. Shaders never import this module themselves.
import strafe.surface;

[shader("vertex")]
Surface standardVertexMain(uint vertex_id : SV_VulkanVertexID) {
    return standardVertex(vertex_id);
}
