// Lighting from the frame's light
import strafe.draw;
import strafe.surface;

// Lambert diffuse from the directional light, plus the ambient term.
float3 lambert(Surface surface, float3 albedo) {
    let f = frame();
    let normal = normalize(surface.world_normal);
    let n_dot_l = max(dot(normal, -normalize(f.light_direction)), 0.0);
    return albedo * (f.ambient + f.light_color * n_dot_l);
}
