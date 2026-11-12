#include "week3.h"

#include "strafe/core/log.h"
#include "strafe/engine.h"
#include "strafe/renderer/camera.h"
#include "strafe/renderer/frame_desc.h"
#include "strafe/renderer/obj_loader.h"
#include "strafe/renderer/renderer.h"
#include "strafe/renderer/texture_loader.h"

#include <optional>

#include <glm/gtc/matrix_transform.hpp>

using namespace strafe;

namespace {

// One redead per material, left to right.
constexpr glm::vec3 kRestingRedeadPosition(-3.0f, -1.75f, 0.0f);
constexpr glm::vec3 kGhostPosition(-1.0f, -1.75f, 0.0f);
constexpr glm::vec3 kUnlitPosition(1.0f, -1.75f, 0.0f);
constexpr glm::vec3 kNormalsPosition(3.0f, -1.75f, 0.0f);

constexpr float kSpinSpeed = 1.0f; // Radians per second.

constexpr glm::vec3 kCameraPosition(0.0f, 2.0f, 6.5f);
constexpr glm::vec3 kCameraTarget(0.0f, 0.2f, -1.0f);
constexpr float kNearPlane = 0.1f;
constexpr float kFarPlane = 100.0f;
constexpr float kOrthographicHeight = 6.0f;

void drawMesh(
    renderer::FrameDesc &frame, renderer::MeshId mesh, renderer::MaterialId material, const glm::mat4 &transform) {
  frame.draws.push_back({.mesh = mesh, .material = material, .transform = transform});
}

glm::mat4 translation(glm::vec3 position) { return glm::translate(glm::mat4(1.0f), position); }

} // namespace

bool Week3::onInit(Engine &engine) {
  renderer::Renderer &renderer = engine.renderer();

  std::optional<renderer::MeshId> redead;
  if (auto redead_data = renderer::loadFromObj("assets/models/redead.obj")) {
    redead = renderer.createMesh(redead_data->vertices, redead_data->indices);
  }

  if (auto tex = renderer::loadTexture("assets/textures/redead_grp.png")) {
    redead_texture_ = *renderer.createTexture(*tex, renderer::ColorSpace::Srgb);
  }
  if (!redead) {
    log::error("Failed to load the redead mesh");
    return false;
  }
  redead_ = *redead;

  const std::optional<gfx::ShaderId> lit = renderer.loadShader("lit");
  const std::optional<gfx::ShaderId> unlit = renderer.loadShader("unlit");
  const std::optional<gfx::ShaderId> normals = renderer.loadShader("normals_debug");
  if (!lit || !unlit || !normals) {
    return false;
  }
  const std::optional<renderer::MaterialId> redead_material = renderer.createMaterial(*lit);
  const std::optional<renderer::MaterialId> ghost_material = renderer.createMaterial(*lit);
  const std::optional<renderer::MaterialId> unlit_material = renderer.createMaterial(*unlit);
  const std::optional<renderer::MaterialId> normals_material = renderer.createMaterial(*normals);
  if (!redead_material || !ghost_material || !unlit_material || !normals_material) {
    return false;
  }
  redead_material_ = *redead_material;
  ghost_material_ = *ghost_material;
  unlit_material_ = *unlit_material;
  normals_material_ = *normals_material;

  renderer.setParam(redead_material_, "albedo", redead_texture_);
  renderer.setParam(unlit_material_, "albedo", redead_texture_);

  renderer.setParam(ghost_material_, "tint", glm::vec4(1.0f, 0.35f, 0.3f, 0.5f));
  renderer.setParam(ghost_material_, "albedo", redead_texture_);
  renderer.setParam(ghost_material_, "sampler", gfx::Sampler::NearestRepeat);
  return true;
}

void Week3::onEvent(Engine &engine, const SDL_Event &event) {
  if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && event.key.key == SDLK_P) {
    perspective_ = !perspective_;
  }
}

void Week3::onUpdate(Engine &engine, float delta_seconds) { time_ += delta_seconds; }

void Week3::onRender(Engine &engine, renderer::FrameDesc &frame) {
  const float aspect = engine.aspect();
  const glm::mat4 view = glm::lookAt(kCameraPosition, kCameraTarget, glm::vec3(0.0f, 1.0f, 0.0f));
  const glm::mat4 projection = perspective_
      ? renderer::perspective(glm::radians(60.0f), aspect, kNearPlane, kFarPlane)
      : renderer::orthographic(kOrthographicHeight, aspect, kNearPlane, kFarPlane);
  frame.view_projection = projection * view;
  frame.camera_position = kCameraPosition;

  const glm::vec3 spin_axis(0.0f, 1.0f, 0.0f);
  const auto spinning = [&](glm::vec3 position) {
    return glm::scale(glm::rotate(translation(position), time_ * kSpinSpeed, spin_axis), glm::vec3(0.025f));
  };
  drawMesh(frame, redead_, redead_material_, spinning(kRestingRedeadPosition));
  drawMesh(frame, redead_, ghost_material_, spinning(kGhostPosition));
  drawMesh(frame, redead_, unlit_material_, spinning(kUnlitPosition));
  drawMesh(frame, redead_, normals_material_, spinning(kNormalsPosition));
}
