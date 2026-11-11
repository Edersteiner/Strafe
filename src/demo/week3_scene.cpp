#include "demo/week3_scene.h"

#include "renderer/camera.h"
#include "renderer/mesh.h"
#include "renderer/obj_loader.h"
#include "renderer/renderer.h"
#include "renderer/texture_loader.h"

#include <optional>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

namespace strafe::demo {

namespace {

constexpr glm::vec3 kRestingRedeadPosition(-1.5f, -1.75f, 0.0f);
constexpr glm::vec3 kGhostPosition(1.5f, -1.75f, 0.0f);

constexpr float kSpinSpeed = 1.0f; // Radians per second.

constexpr glm::vec3 kCameraPosition(0.0f, 2.0f, 6.5f);
constexpr glm::vec3 kCameraTarget(0.0f, 0.2f, -1.0f);
constexpr float kNearPlane = 0.1f;
constexpr float kFarPlane = 100.0f;
constexpr float kOrthographicHeight = 6.0f;

void drawMesh(renderer::FrameDesc &frame, renderer::MeshId mesh,
              renderer::MaterialId material, const glm::mat4 &transform) {
  frame.draws.push_back(
      {.mesh = mesh, .material = material, .transform = transform});
}

glm::mat4 translation(glm::vec3 position) {
  return glm::translate(glm::mat4(1.0f), position);
}
} // namespace

bool createSceneMeshes(renderer::Renderer &renderer, Scene &scene) {
  std::optional<renderer::MeshId> redead;
  if (auto redead_data = renderer::loadFromObj("assets/models/redead.obj")) {
    redead = renderer.createMesh(redead_data->vertices, redead_data->indices);
  }

  if (auto tex = renderer::loadTexture("assets/textures/redead_grp.png")) {
    scene.redead_texture =
        *renderer.createTexture(*tex, renderer::ColorSpace::Srgb);
  }
  if (!redead) {
    return false;
  }
  scene.redead = *redead;

  const std::optional<gfx::ShaderId> lit = renderer.loadShader("lit");
  if (!lit) {
    return false;
  }
  const std::optional<renderer::MaterialId> redead_material =
      renderer.createMaterial(*lit);
  const std::optional<renderer::MaterialId> ghost_material =
      renderer.createMaterial(*lit);
  if (!redead_material || !ghost_material) {
    return false;
  }
  scene.redead_material = *redead_material;
  scene.ghost_material = *ghost_material;

  renderer.setParam(scene.redead_material, "tint", glm::vec4(1.0f));
  renderer.setParam(scene.redead_material, "albedo", scene.redead_texture);

  renderer.setParam(scene.ghost_material, "tint",
                    glm::vec4(1.0f, 0.35f, 0.3f, 0.5f));
  renderer.setParam(scene.ghost_material, "albedo", scene.redead_texture);
  renderer.setParam(scene.ghost_material, "sampler",
                    gfx::Sampler::NearestRepeat);
  return true;
}

void toggleProjection(Scene &scene) { scene.perspective = !scene.perspective; }

void updateScene(Scene &scene, float delta_seconds) {
  scene.time += delta_seconds;
}

void buildScene(renderer::FrameDesc &frame, const Scene &scene, float aspect) {
  const glm::mat4 view =
      glm::lookAt(kCameraPosition, kCameraTarget, glm::vec3(0.0f, 1.0f, 0.0f));
  const glm::mat4 projection =
      scene.perspective ? renderer::perspective(glm::radians(60.0f), aspect,
                                                kNearPlane, kFarPlane)
                        : renderer::orthographic(kOrthographicHeight, aspect,
                                                 kNearPlane, kFarPlane);
  frame.view_projection = projection * view;
  frame.camera_position = kCameraPosition;
  frame.time = scene.time;

  const glm::vec3 spin_axis = glm::normalize(glm::vec3(0.0f, 1.0f, 0.0f));
  const auto spinning = [&](glm::vec3 position) {
    return glm::scale(
        glm::rotate(translation(position), scene.time * kSpinSpeed, spin_axis),
        glm::vec3(0.025f));
  };
  drawMesh(frame, scene.redead, scene.redead_material,
           spinning(kRestingRedeadPosition));
  drawMesh(frame, scene.redead, scene.ghost_material, spinning(kGhostPosition));
}

} // namespace strafe::demo
