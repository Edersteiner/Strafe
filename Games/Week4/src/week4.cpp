#include "week4.h"

#include "strafe/core/log.h"
#include "strafe/core/math.h"
#include "strafe/engine.h"
#include "strafe/input/input_manager.h"
#include "strafe/renderer/camera.h"
#include "strafe/renderer/frame_desc.h"
#include "strafe/renderer/primitives.h"
#include "strafe/renderer/renderer.h"

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

std::optional<renderer::MaterialId> createTinted(renderer::Renderer &renderer, gfx::ShaderId shader, Vec4 tint) {
  const std::optional<renderer::MaterialId> material = renderer.createMaterial(shader);
  if (material) {
    renderer.setParam(*material, "tint", tint);
  }
  return material;
}

} // namespace

bool Week4::onInit(Engine &engine) {
  input::InputManager &input = engine.input();

  FlyCamera::bindActions(input);

  input.bind("Quit", {input::Key::Escape});
  input.bind("CaptureMouse", {input::MouseButton::Left});
  input.bind("ToggleSplitscreen", {input::Key::Tab, input::GamepadButton::Back});
  input.bind("NextScene", {input::Key::Right, input::GamepadButton::DpadRight});
  input.bind("PreviousScene", {input::Key::Left, input::GamepadButton::DpadLeft});
  input.bind("ReloadScene", {input::Key::R});

  renderer::Renderer &renderer = engine.renderer();
  const std::optional<gfx::ShaderId> lit = renderer.loadShader("lit");
  if (!lit) {
    return false;
  }
  for (size_t i = 0; i < player_materials_.size(); ++i) {
    const std::optional<renderer::MaterialId> material = createTinted(renderer, *lit, kPlayerTints[i]);
    if (!material) {
      return false;
    }
    player_materials_[i] = *material;
  }
  const std::optional<renderer::MaterialId> goal_material = createTinted(renderer, *lit, kGoalTint);
  if (!goal_material) {
    return false;
  }
  goal_material_ = *goal_material;

  if (!loadScene(0)) {
    return false;
  }
  setMouseCaptured(engine, true);
  return true;
}

void Week4::onUpdate(Engine &engine, float delta_seconds) {
  input::InputManager &input = engine.input();
  if (input.isActionJustPressed("Quit")) {
    if (mouse_captured_) {
      setMouseCaptured(engine, false);
    } else {
      engine.quit();
    }
  } else if (!mouse_captured_ && input.isActionJustPressed("CaptureMouse")) {
    setMouseCaptured(engine, true);
  }

  // Keyboard and mouse stay with player 0, the first gamepad moves to player 1.
  if (input.isActionJustPressed("ToggleSplitscreen")) {
    input.setPlayerCount(input.playerCount() == 1 ? kPlayerCount : 1);
  }

  if (input.isActionJustPressed("NextScene")) {
    loadScene((scene_index_ + 1) % kScenePaths.size());
  } else if (input.isActionJustPressed("PreviousScene")) {
    loadScene((scene_index_ + kScenePaths.size() - 1) % kScenePaths.size());
  } else if (input.isActionJustPressed("ReloadScene")) {
    loadScene(scene_index_);
  }

  for (uint8_t player = 0; player < input.playerCount(); ++player) {
    cameras_[player].update(input, player, delta_seconds, mouse_captured_ && player == 0);
  }

  if (const scene::Marker *goal = scene_.findMarker("Goal")) {
    for (uint8_t player = 0; player < input.playerCount(); ++player) {
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

void Week4::setMouseCaptured(Engine &engine, bool captured) {
  engine.input().setRelativeMouseMode(captured);
  mouse_captured_ = captured;
}

void Week4::onRender(Engine &engine, renderer::FrameDesc &frame) {
  renderer::Renderer &renderer = engine.renderer();
  const uint8_t player_count = engine.input().playerCount();
  if (player_count == 1) {
    frame.camera = cameraFor(cameras_[0]);
  } else {
    frame.views.push_back({.id = {0}, .camera = cameraFor(cameras_[0]), .region = renderer::ScreenRegion::left()});
    frame.views.push_back({.id = {1}, .camera = cameraFor(cameras_[1]), .region = renderer::ScreenRegion::right()});
  }

  scene_renderer_.draw(renderer, scene_, frame);

  if (const scene::Marker *goal = scene_.findMarker("Goal")) {
    frame.draws.push_back(
        {.mesh = renderer.primitive(renderer::Primitive::Sphere),
         .material = goal_material_,
         .transform = {.position = goal->transform.position, .scale = Vec3(2.0f * goal->transform.scale.x)}});
  }

  for (uint8_t player = 0; player < player_count; ++player) {
    const FlyCamera &camera = cameras_[player];
    const Quat rotation = camera.rotation();
    const Quat facing = Quat::fromAxisAngle(Vec3(0.0f, 1.0f, 0.0f), kPi);
    renderer.renderObj(
        frame, kPlayerModel, player_materials_[player],
        {.position = camera.position + rotation * Vec3(0.0f, 0.0f, kMarkerBackOffset),
         .rotation = rotation * facing,
         .scale = Vec3(kMarkerSize)});
  }
}
