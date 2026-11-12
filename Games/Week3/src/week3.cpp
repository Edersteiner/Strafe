#include "week3.h"

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
constexpr glm::vec3 kRestingRedeadPosition(-3.0f, -1.0f, 0.0f);

constexpr float kSpinSpeed = 1.0f; // Radians per second.

constexpr glm::vec3 kCameraPosition(0.0f, 2.0f, 6.5f);
constexpr glm::vec3 kCameraTarget(0.0f, 0.0f, 2.0f);
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

  std::optional<renderer::MeshId> grave;
  if (auto grave_data = renderer::loadFromObj("assets/models/HeartPieceGrave.obj")) {
    grave = renderer.createMesh(grave_data->vertices, grave_data->indices);
  }

  std::optional<renderer::MeshId> link;
  if (auto link_data = renderer::loadFromObj("assets/models/link_v2.obj")) {
    link = renderer.createMesh(link_data->vertices, link_data->indices);
  }

  std::optional<renderer::MeshId> poe;
  if (auto poe_data = renderer::loadFromObj("assets/models/poe.obj")) {
    poe = renderer.createMesh(poe_data->vertices, poe_data->indices);
  }

  if (auto tex = renderer::loadTexture("assets/textures/redead_grp.png")) {
    redead_texture_ = *renderer.createTexture(*tex, renderer::ColorSpace::Srgb);
  }
  if (auto tex = renderer::loadTexture("assets/textures/HeartPieceGrave.png")) {
    grave_texture_ = *renderer.createTexture(*tex, renderer::ColorSpace::Srgb);
  }
  if (auto tex = renderer::loadTexture("assets/textures/link_v2.png")) {
    link_texture_ = *renderer.createTexture(*tex, renderer::ColorSpace::Srgb);
  }
  if (auto tex = renderer::loadTexture("assets/textures/poe_grp.png")) {
    poe_texture_ = *renderer.createTexture(*tex, renderer::ColorSpace::Srgb);
  }

  redead_ = *redead;
  grave_ = *grave;
  link_ = *link;
  poe_ = *poe;

  const std::optional<gfx::ShaderId> lit = renderer.loadShader("lit");

  const std::optional<renderer::MaterialId> redead_material = renderer.createMaterial(*lit);
  const std::optional<renderer::MaterialId> grave_material = renderer.createMaterial(*lit);
  const std::optional<renderer::MaterialId> link_material = renderer.createMaterial(*lit);
  const std::optional<renderer::MaterialId> poe_material = renderer.createMaterial(*lit);

  redead_material_ = *redead_material;
  grave_material_ = *grave_material;
  link_material_ = *link_material;
  poe_material_ = *poe_material;

  renderer.setParam(redead_material_, "albedo", redead_texture_);
  renderer.setParam(grave_material_, "albedo", grave_texture_);
  renderer.setParam(link_material_, "albedo", link_texture_);
  renderer.setParam(poe_material_, "albedo", poe_texture_);
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
  glm::mat4 view = glm::lookAt(
      glm::vec3(5 * sin(time_ * 0.25f), 2.5f, 5 * cos(time_ * 0.25f)), kCameraTarget, glm::vec3(0.0f, 1.0f, 0.0f));
  const glm::mat4 projection = perspective_
      ? renderer::perspective(glm::radians(60.0f), aspect, kNearPlane, kFarPlane)
      : renderer::orthographic(kOrthographicHeight, aspect, kNearPlane, kFarPlane);

  frame.view_projection = projection * view;
  frame.camera_position = kCameraPosition;

  const glm::vec3 light_position(0.0f, 2.5f, 0.0f);
  const glm::vec3 light_target(5 * std::sin(time_ * 2.5f), 0.0f, 5 * std::cos(time_ * 2.5f));
  frame.light.direction = glm::normalize(light_target - light_position);

  frame.light.ambient = glm::vec3(0.1f, 0.01f, 0.01f);
  frame.light.color = glm::vec3(0.95f, 0.2f, 0.5f);

  const glm::vec3 spin_axis(0.0f, 1.0f, 0.0f);
  drawMesh(
      frame, redead_, redead_material_,
      glm::rotate(
          glm::scale(glm::translate(glm::mat4(1.0f), kRestingRedeadPosition), glm::vec3(0.015f)), glm::radians(45.0f),
          glm::vec3(0.0f, 1.0f, 0.0f)));
  drawMesh(
      frame, grave_, grave_material_,
      glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 10.0f)), glm::vec3(5.0f)));
  drawMesh(
      frame, link_, link_material_,
      glm::rotate(
          glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -1.9f, 2.0f)), glm::vec3(0.5f)),
          glm::radians(45.0f), glm::vec3(0.0f, -1.0f, 0.0f)));
  drawMesh(
      frame, poe_, poe_material_,
      glm::rotate(
          glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(5.0f, 0.5f * sin(time_ * 2.0f), 5.0f)), glm::vec3(1.0f)),
          glm::radians(240.0f), glm::vec3(0.1f * sin(time_), 1.0f, 0.1f * sin(time_ * 4.0f))));
}
