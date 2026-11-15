#include "week3.h"

#include "strafe/core/math.h"
#include "strafe/engine.h"
#include "strafe/renderer/camera.h"
#include "strafe/renderer/frame_desc.h"
#include "strafe/renderer/renderer.h"
#include "strafe/renderer/texture_loader.h"

#include <cmath>
#include <optional>

using namespace strafe;

namespace {

constexpr Vec3 kCameraPosition(0.0f, 2.0f, 6.5f);
constexpr Vec3 kCameraTarget(0.0f, 0.0f, 2.0f);
constexpr float kNearPlane = 0.1f;
constexpr float kFarPlane = 100.0f;
constexpr float kOrthographicHeight = 6.0f;

} // namespace

bool Week3::onInit(Engine &engine) {
  renderer::Renderer &renderer = engine.renderer();

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

  if (auto tex = renderer::loadTexture("assets/textures/poe_collector_grp.png")) {
    poe_collector_texture_ = *renderer.createTexture(*tex, renderer::ColorSpace::Srgb);
  }

  const std::optional<gfx::ShaderId> lit = renderer.loadShader("lit");

  redead_material_ = *renderer.createMaterial(*lit);
  grave_material_ = *renderer.createMaterial(*lit);
  link_material_ = *renderer.createMaterial(*lit);
  poe_material_ = *renderer.createMaterial(*lit);
  poe_collector_material_ = *renderer.createMaterial(*lit);
  suzanne_material_ = *renderer.createMaterial(*lit);

  renderer.setParam(redead_material_, "albedo", redead_texture_);
  renderer.setParam(grave_material_, "albedo", grave_texture_);
  renderer.setParam(link_material_, "albedo", link_texture_);
  renderer.setParam(poe_material_, "albedo", poe_texture_);
  renderer.setParam(poe_collector_material_, "albedo", poe_collector_texture_);
  // Untextured, so albedo stays the default white and the tint is the colour.
  renderer.setParam(suzanne_material_, "tint", Vec4(0.8f, 0.8f, 0.8f, 1.0f));
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
  const Vec3 eye(5.0f * std::sin(time_ * 0.25f), 2.5f, 5.0f * std::cos(time_ * 0.25f));
  const Mat4 view = lookAt(eye, kCameraTarget, Vec3(0.0f, 1.0f, 0.0f));
  const Mat4 projection = perspective_ ? renderer::perspective(radians(60.0f), aspect, kNearPlane, kFarPlane)
                                       : renderer::orthographic(kOrthographicHeight, aspect, kNearPlane, kFarPlane);

  frame.view_projection = projection * view;
  frame.camera_position = kCameraPosition;

  const Vec3 light_position(0.0f, 2.5f, 0.0f);
  const Vec3 light_target(5.0f * std::sin(time_ * 2.5f), 0.0f, 5.0f * std::cos(time_ * 2.5f));
  frame.light.direction = normalize(light_target - light_position);

  frame.light.ambient = Vec3(0.1f, 0.01f, 0.01f);
  frame.light.color = Vec3(0.95f, 0.2f, 0.5f);

  renderer::Renderer &renderer = engine.renderer();

  renderer.renderObj(
      frame, "assets/models/redead.obj", redead_material_,
      {.position = Vec3(-3.0f, -1.0f, 0.0f),
       .rotation = Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, radians(45.0f)),
       .scale = Vec3(0.015f)});
  renderer.renderObj(
      frame, "assets/models/redead.obj", redead_material_,
      {.position = Vec3(3.0f, -1.0f, 0.0f),
       .rotation = Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, radians(-45.0f)),
       .scale = Vec3(0.015f)});

  renderer.renderObj(
      frame, "assets/models/HeartPieceGrave.obj", grave_material_,
      {.position = {0.0f, 0.0f, 10.0f}, .scale = Vec3(5.0f)});

  renderer.renderObj(
      frame, "assets/models/link_v2.obj", link_material_,
      {.position = {0.0f, -1.9f, 2.0f},
       .rotation = Quat::fromAxisAngle({0.0f, -1.0f, 0.0f}, radians(45.0f)),
       .scale = Vec3(0.5f)});

  const Vec3 poe_axis(0.1f * std::sin(time_), 1.0f, 0.1f * std::sin(time_ * 4.0f));
  renderer.renderObj(
      frame, "assets/models/poe.obj", poe_material_,
      {.position = {5.0f, 0.5f * std::sin(time_ * 2.0f), 5.0f},
       .rotation = Quat::fromAxisAngle(poe_axis, radians(240.0f))});

  renderer.renderObj(
      frame, "assets/models/poe_collector_sitting.obj", poe_collector_material_,
      {.position = {0.0f, -1.3f, -1.0f}, .scale = Vec3(0.02f)});

  renderer.renderObj(
      frame, "assets/models/suzanne.obj", suzanne_material_,
      {.position = {-3.0f, 2.2f, 20.0f}, .rotation = Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, radians(180.0f))});
}
