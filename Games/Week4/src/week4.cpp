#include "week4.h"

#include "strafe/core/math.h"
#include "strafe/engine.h"
#include "strafe/input/input_manager.h"
#include "strafe/renderer/camera.h"
#include "strafe/renderer/frame_desc.h"
#include "strafe/renderer/renderer.h"

#include <array>
#include <cstdint>
#include <optional>

using namespace strafe;

namespace {

constexpr std::array<Vec3, 2> kStartPositions{{{0.0f, 1.5f, 4.0f}, {0.0f, 1.5f, -4.0f}}};
constexpr std::array<Vec4, 2> kPlayerTints{{{0.2f, 0.5f, 0.95f, 1.0f}, {0.95f, 0.25f, 0.3f, 1.0f}}};

constexpr float kMarkerSize = 0.3f;

renderer::Camera cameraFor(const FlyCamera &fly_camera) {
  return {
      .transform = {.position = fly_camera.position, .rotation = fly_camera.rotation()},
      .projection = renderer::Perspective{.fov_y = radians(60.0f), .far_plane = 100.0f}};
}

// A unit cube centred on the origin, with one flat-shaded quad per face.
renderer::MeshData cubeMeshData() {
  struct Face {
    Vec3 normal, u, v; // u x v == normal, so the corners wind CCW.
  };
  constexpr std::array<Face, 6> kFaces{{
      {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}},
      {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
      {{0, 1, 0}, {0, 0, 1}, {1, 0, 0}},
      {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
      {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},
      {{0, 0, -1}, {0, 1, 0}, {1, 0, 0}},
  }};
  constexpr std::array<Vec2, 4> kCorners{{{-1, -1}, {1, -1}, {1, 1}, {-1, 1}}};

  renderer::MeshData mesh;
  for (const Face &face : kFaces) {
    const auto first = static_cast<uint32_t>(mesh.vertices.size());
    for (const Vec2 corner : kCorners) {
      mesh.vertices.push_back(
          {.position = 0.5f * (face.normal + corner.x * face.u + corner.y * face.v),
           .uv = 0.5f * (corner + Vec2(1.0f)),
           .normal = face.normal});
    }
    for (const uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) {
      mesh.indices.push_back(first + i);
    }
  }
  return mesh;
}

} // namespace

bool Week4::onInit(Engine &engine) {
  // Actions are named by the game. One bind lists every input that triggers it.
  input::InputManager &input = engine.input();
  FlyCamera::bindActions(input);
  // Escape frees a captured mouse first, and quits once it is free.
  input.bind("Quit", {input::Key::Escape});
  input.bind("CaptureMouse", {input::MouseButton::Left});
  input.bind("ToggleSplitscreen", {input::Key::Tab, input::GamepadButton::Back});

  for (size_t i = 0; i < cameras_.size(); ++i) {
    cameras_[i].position = kStartPositions[i];
    cameras_[i].lookAt(Vec3());
  }
  setMouseCaptured(engine, true);

  renderer::Renderer &renderer = engine.renderer();

  const renderer::MeshData cube = cubeMeshData();
  const std::optional<renderer::MeshId> mesh = renderer.createMesh(cube.vertices, cube.indices);
  // "lit" is one of the engine's shaders. An app shader with the same name in
  // shaders/ would replace it.
  const std::optional<gfx::ShaderId> lit = renderer.loadShader("lit");
  if (!mesh || !lit) {
    return false;
  }
  const std::optional<renderer::MaterialId> material = renderer.createMaterial(*lit);
  if (!material) {
    return false;
  }
  cube_ = *mesh;
  material_ = *material;

  // Fields of LitParams in lit.slang, by name. "albedo" is left unset, so it
  // samples the 1x1 white texture.
  renderer.setParam(material_, "tint", Vec4(0.9f, 0.55f, 0.2f, 1.0f));

  for (size_t i = 0; i < player_materials_.size(); ++i) {
    const std::optional<renderer::MaterialId> player_material = renderer.createMaterial(*lit);
    if (!player_material) {
      return false;
    }
    player_materials_[i] = *player_material;
    renderer.setParam(player_materials_[i], "tint", kPlayerTints[i]);
  }
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

  for (uint8_t player = 0; player < input.playerCount(); ++player) {
    cameras_[player].update(input, player, delta_seconds, mouse_captured_ && player == 0);
  }
}

void Week4::setMouseCaptured(Engine &engine, bool captured) {
  engine.input().setRelativeMouseMode(captured);
  mouse_captured_ = captured;
}

void Week4::onRender(Engine &engine, renderer::FrameDesc &frame) {
  const uint8_t player_count = engine.input().playerCount();
  if (player_count == 1) {
    frame.camera = cameraFor(cameras_[0]);
  } else {
    frame.views.push_back({.id = {0}, .camera = cameraFor(cameras_[0]), .region = renderer::ScreenRegion::left()});
    frame.views.push_back({.id = {1}, .camera = cameraFor(cameras_[1]), .region = renderer::ScreenRegion::right()});
  }

  frame.draws.push_back({.mesh = cube_, .material = material_});

  for (uint8_t player = 0; player < player_count; ++player) {
    const FlyCamera &camera = cameras_[player];
    frame.draws.push_back(
        {.mesh = cube_,
         .material = player_materials_[player],
         .transform = {.position = camera.position, .rotation = camera.rotation(), .scale = Vec3(kMarkerSize)}});
  }
}
