#include "week3.h"

#include "strafe/core/math.h"
#include "strafe/input.h"
#include "strafe/renderer.h"
#include "strafe/renderer/camera.h"
#include "strafe/renderer/texture_loader.h"

#include <cmath>
#include <optional>

using namespace strafe;

namespace {

constexpr Vec3 kCameraTarget(0.0f, 0.0f, 2.0f);
constexpr float kNearPlane = 0.1f;
constexpr float kFarPlane = 100.0f;
constexpr float kOrthographicHeight = 6.0f;

void cycleDebugView() {
  using strafe::renderer::DebugView;
  const DebugView current = Renderer::debugView();
  const auto next = static_cast<DebugView>((static_cast<int>(current) + 1) % static_cast<int>(DebugView::Count));
  Renderer::setDebugView(next);
}

} // namespace

bool Week3::onInit() {
  Input::bind("ToggleProjection", {input::Key::P, input::GamepadButton::North});
  Input::bind("CycleDebugView", {input::Key::D, input::GamepadButton::West});

  if (auto tex = renderer::loadTexture("assets/textures/redead_grp.png")) {
    redead_texture_ = *Renderer::createTexture(*tex, renderer::ColorSpace::Srgb);
  }
  if (auto tex = renderer::loadTexture("assets/textures/HeartPieceGrave.png")) {
    grave_texture_ = *Renderer::createTexture(*tex, renderer::ColorSpace::Srgb);
  }
  if (auto tex = renderer::loadTexture("assets/textures/link_v2.png")) {
    link_texture_ = *Renderer::createTexture(*tex, renderer::ColorSpace::Srgb);
  }
  if (auto tex = renderer::loadTexture("assets/textures/poe_grp.png")) {
    poe_texture_ = *Renderer::createTexture(*tex, renderer::ColorSpace::Srgb);
  }

  if (auto tex = renderer::loadTexture("assets/textures/poe_collector_grp.png")) {
    poe_collector_texture_ = *Renderer::createTexture(*tex, renderer::ColorSpace::Srgb);
  }

  const std::optional<gfx::ShaderId> lit = Renderer::loadShader("lit");

  redead_material_ = *Renderer::createMaterial(*lit);
  grave_material_ = *Renderer::createMaterial(*lit);
  link_material_ = *Renderer::createMaterial(*lit);
  poe_material_ = *Renderer::createMaterial(*lit);
  poe_collector_material_ = *Renderer::createMaterial(*lit);
  suzanne_material_ = *Renderer::createMaterial(*lit);

  Renderer::setParam(redead_material_, "albedo", redead_texture_);
  Renderer::setParam(grave_material_, "albedo", grave_texture_);
  Renderer::setParam(link_material_, "albedo", link_texture_);
  Renderer::setParam(poe_material_, "albedo", poe_texture_);
  Renderer::setParam(poe_collector_material_, "albedo", poe_collector_texture_);
  // Untextured, so albedo stays the default white and the tint is the colour.
  Renderer::setParam(suzanne_material_, "tint", Vec4(0.8f, 0.8f, 0.8f, 1.0f));
  return true;
}

void Week3::onUpdate(float delta_seconds) {
  time_ += delta_seconds;

  if (Input::isActionJustPressed("ToggleProjection")) {
    perspective_ = !perspective_;
  }
  if (Input::isActionJustPressed("CycleDebugView")) {
    cycleDebugView();
  }
}

void Week3::onRender() {
  renderer::FrameDesc &frame = Renderer::frame();
  renderer::Camera &camera = frame.camera;
  camera.transform.position = Vec3(5.0f * std::sin(time_ * 0.25f), 2.5f, 5.0f * std::cos(time_ * 0.25f));
  camera.lookAt(kCameraTarget);
  camera.projection = perspective_
      ? renderer::Projection{renderer::Perspective{
            .fov_y = radians(60.0f), .near_plane = kNearPlane, .far_plane = kFarPlane}}
      : renderer::Projection{
            renderer::Orthographic{.height = kOrthographicHeight, .near_plane = kNearPlane, .far_plane = kFarPlane}};

  const Vec3 light_position(0.0f, 2.5f, 0.0f);
  const Vec3 light_target(5.0f * std::sin(time_ * 2.5f), 0.0f, 5.0f * std::cos(time_ * 2.5f));
  frame.light.direction = normalize(light_target - light_position);

  frame.light.ambient = Vec3(0.1f, 0.01f, 0.01f);
  frame.light.color = Vec3(0.95f, 0.2f, 0.5f);

  Renderer::renderObj(
      "assets/models/redead.obj", redead_material_,
      {.position = Vec3(-3.0f, -1.0f, 0.0f),
       .rotation = Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, radians(45.0f)),
       .scale = Vec3(0.015f)});
  Renderer::renderObj(
      "assets/models/redead.obj", redead_material_,
      {.position = Vec3(3.0f, -1.0f, 0.0f),
       .rotation = Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, radians(-45.0f)),
       .scale = Vec3(0.015f)});

  Renderer::renderObj(
      "assets/models/HeartPieceGrave.obj", grave_material_, {.position = {0.0f, 0.0f, 10.0f}, .scale = Vec3(5.0f)});

  Renderer::renderObj(
      "assets/models/link_v2.obj", link_material_,
      {.position = {0.0f, -1.9f, 2.0f},
       .rotation = Quat::fromAxisAngle({0.0f, -1.0f, 0.0f}, radians(45.0f)),
       .scale = Vec3(0.5f)});

  const Vec3 poe_axis(0.1f * std::sin(time_), 1.0f, 0.1f * std::sin(time_ * 4.0f));
  Renderer::renderObj(
      "assets/models/poe.obj", poe_material_,
      {.position = {5.0f, 0.5f * std::sin(time_ * 2.0f), 5.0f},
       .rotation = Quat::fromAxisAngle(poe_axis, radians(240.0f))});

  Renderer::renderObj(
      "assets/models/poe_collector_sitting.obj", poe_collector_material_,
      {.position = {0.0f, -1.3f, -1.0f}, .scale = Vec3(0.02f)});

  Renderer::renderObj(
      "assets/models/suzanne.obj", suzanne_material_,
      {.position = {-3.0f, 2.2f, 20.0f}, .rotation = Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, radians(180.0f))});
}
