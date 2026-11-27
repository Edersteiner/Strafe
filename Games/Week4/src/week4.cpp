#include "week4.h"

#include "strafe/core/log.h"
#include "strafe/core/math.h"
#include "strafe/engine.h"
#include "strafe/input.h"
#include "strafe/renderer.h"
#include "strafe/renderer/camera.h"

#include <array>
#include <cstdint>
#include <optional>

using namespace strafe;

namespace {

constexpr std::array<const char *, 3> kScenePaths{
    "assets/scenes/courtyard.scene",
    "assets/scenes/corridors.scene",
    "assets/scenes/shaft.scene",
};

constexpr const char *kPlayerModel = "assets/models/suzanne.obj";
constexpr std::array<Vec4, 2> kPlayerTints{{{0.2f, 0.5f, 0.95f, 1.0f}, {0.95f, 0.25f, 0.3f, 1.0f}}};
constexpr Vec4 kGoalTint(1.0f, 0.8f, 0.2f, 1.0f);

constexpr float kMarkerSize = 0.3f;
constexpr float kMarkerBackOffset = 0.35f;

renderer::Camera cameraFor(const FlyCamera &fly_camera) {
  return {
      .transform = {.position = fly_camera.position, .rotation = fly_camera.rotation()},
      .projection = renderer::Perspective{.fov_y = radians(60.0f), .far_plane = 100.0f}};
}

std::optional<renderer::MaterialId> createTinted(gfx::ShaderId shader, Vec4 tint) {
  const std::optional<renderer::MaterialId> material = Renderer::createMaterial(shader);
  if (material) {
    Renderer::setParam(*material, "tint", tint);
  }
  return material;
}

} // namespace

bool Week4::onInit() {
  FlyCamera::bindActions();

  Input::bind("Quit", {input::Key::Escape});
  Input::bind("CaptureMouse", {input::MouseButton::Left});
  Input::bind("ToggleSplitscreen", {input::Key::Tab, input::GamepadButton::Back});
  Input::bind("NextScene", {input::Key::Right, input::GamepadButton::DpadRight});
  Input::bind("PreviousScene", {input::Key::Left, input::GamepadButton::DpadLeft});
  Input::bind("ReloadScene", {input::Key::R});

  const std::optional<gfx::ShaderId> lit = Renderer::loadShader("lit");
  if (!lit) {
    return false;
  }
  for (size_t i = 0; i < player_materials_.size(); ++i) {
    const std::optional<renderer::MaterialId> material = createTinted(*lit, kPlayerTints[i]);
    if (!material) {
      return false;
    }
    player_materials_[i] = *material;
  }
  const std::optional<renderer::MaterialId> goal_material = createTinted(*lit, kGoalTint);
  if (!goal_material) {
    return false;
  }
  goal_material_ = *goal_material;

  if (!loadScene(0)) {
    return false;
  }
  setMouseCaptured(true);
  return true;
}

void Week4::onUpdate(float delta_seconds) {
  if (Input::isActionJustPressed("Quit")) {
    if (mouse_captured_) {
      setMouseCaptured(false);
    } else {
      Engine::quit();
    }
  } else if (!mouse_captured_ && Input::isActionJustPressed("CaptureMouse")) {
    setMouseCaptured(true);
  }

  // Keyboard and mouse stay with player 0, the first gamepad moves to player 1.
  if (Input::isActionJustPressed("ToggleSplitscreen")) {
    Input::setPlayerCount(Input::playerCount() == 1 ? kPlayerCount : 1);
  }

  if (Input::isActionJustPressed("NextScene")) {
    loadScene((scene_index_ + 1) % kScenePaths.size());
  } else if (Input::isActionJustPressed("PreviousScene")) {
    loadScene((scene_index_ + kScenePaths.size() - 1) % kScenePaths.size());
  } else if (Input::isActionJustPressed("ReloadScene")) {
    loadScene(scene_index_);
  }

  for (uint8_t player = 0; player < Input::playerCount(); ++player) {
    cameras_[player].update(player, delta_seconds, mouse_captured_ && player == 0);
  }

  if (const scene::Marker *goal = scene_.findMarker("Goal")) {
    for (uint8_t player = 0; player < Input::playerCount(); ++player) {
      if (length(cameras_[player].position - goal->transform.position) <= goal->transform.scale.x) {
        log::info("Player {} reached the goal of '{}'", player + 1, scene_.name);
        loadScene((scene_index_ + 1) % kScenePaths.size());
        break;
      }
    }
  }
}

bool Week4::loadScene(size_t index) {
  std::optional<scene::Scene> loaded = scene::loadScene(kScenePaths[index]);
  if (!loaded) {
    return false;
  }
  scene_ = std::move(*loaded);
  scene_index_ = index;

  const std::vector<const scene::Marker *> starts = scene_.findMarkers("PlayerStart");
  if (starts.empty()) {
    log::warn("Scene '{}' has no PlayerStart, starting at the origin", scene_.name);
  }
  for (size_t i = 0; i < cameras_.size(); ++i) {
    FlyCamera &camera = cameras_[i];
    const Transform start = starts.empty() ? Transform{} : starts[i % starts.size()]->transform;
    camera.position = start.position;
    camera.lookAt(start.position + start.rotation * Vec3(0.0f, 0.0f, -1.0f));
  }
  return true;
}

void Week4::setMouseCaptured(bool captured) {
  Input::setRelativeMouseMode(captured);
  mouse_captured_ = captured;
}

void Week4::onRender() {
  renderer::FrameDesc &frame = Renderer::frame();
  const uint8_t player_count = Input::playerCount();
  if (player_count == 1) {
    frame.camera = cameraFor(cameras_[0]);
  } else {
    frame.views.push_back({.id = {0}, .camera = cameraFor(cameras_[0]), .region = renderer::ScreenRegion::left()});
    frame.views.push_back({.id = {1}, .camera = cameraFor(cameras_[1]), .region = renderer::ScreenRegion::right()});
  }

  scene_renderer_.draw(scene_);

  if (const scene::Marker *goal = scene_.findMarker("Goal")) {
    Renderer::draw(
        {.mesh = Renderer::primitive(renderer::Primitive::Sphere),
         .material = goal_material_,
         .transform = {.position = goal->transform.position, .scale = Vec3(2.0f * goal->transform.scale.x)}});
  }

  for (uint8_t player = 0; player < player_count; ++player) {
    const FlyCamera &camera = cameras_[player];
    const Quat rotation = camera.rotation();
    const Quat facing = Quat::fromAxisAngle(Vec3(0.0f, 1.0f, 0.0f), kPi);
    Renderer::renderObj(
        kPlayerModel, player_materials_[player],
        {.position = camera.position + rotation * Vec3(0.0f, 0.0f, kMarkerBackOffset),
         .rotation = rotation * facing,
         .scale = Vec3(kMarkerSize)});
  }
}

void Week4::switchScene(size_t index) {
  loadScene(index < kScenePaths.size() ? index : (scene_index_ + 1) % kScenePaths.size());
}
