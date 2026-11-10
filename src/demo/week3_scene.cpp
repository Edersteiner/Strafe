#include "demo/week3_scene.h"

#include "demo/sphere.h"
#include "renderer/camera.h"
#include "renderer/mesh.h"
#include "renderer/obj_loader.h"
#include "renderer/renderer.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

namespace strafe::demo {

namespace {

constexpr glm::vec3 kRestingRedeadPosition(0.0f, -1.75f, 0.0f);

constexpr float kSpinSpeed = 1.0f; // Radians per second.

constexpr glm::vec3 kCameraPosition(0.0f, 2.0f, 6.5f);
constexpr glm::vec3 kCameraTarget(0.0f, 0.2f, -1.0f);
constexpr float kNearPlane = 0.1f;
constexpr float kFarPlane = 100.0f;
constexpr float kOrthographicHeight = 6.0f;

void drawMesh(renderer::FrameDesc &frame, renderer::MeshId mesh,
              const glm::mat4 &transform, glm::vec4 color) {
  frame.draws.push_back({.mesh = mesh, .transform = transform, .color = color});
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

  if (!redead) {
    return false;
  }

  scene.redead = *redead;
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

  const glm::vec3 spin_axis = glm::normalize(glm::vec3(0.0f, 1.0f, 0.0f));
  drawMesh(frame, scene.redead,
           glm::scale(glm::rotate(translation(kRestingRedeadPosition),
                                  scene.time * kSpinSpeed, spin_axis),
                      glm::vec3(0.025f)),
           glm::vec4(0.0f));
}

} // namespace strafe::demo
