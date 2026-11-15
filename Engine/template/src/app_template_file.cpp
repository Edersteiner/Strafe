#include "app_template_file.h"

#include "strafe/core/log.h"
#include "strafe/core/math.h"
#include "strafe/engine.h"
#include "strafe/renderer/camera.h"
#include "strafe/renderer/frame_desc.h"
#include "strafe/renderer/renderer.h"

#include <array>
#include <cstdint>
#include <optional>

using namespace strafe;

namespace {

constexpr Vec3 kCameraPosition(0.0f, 1.5f, 4.0f);
constexpr float kSpinSpeed = 0.8f; // Radians per second.

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

bool AppTemplateName::onInit(Engine &engine) {
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
  return true;
}

void AppTemplateName::onEvent(Engine &engine, const SDL_Event &event) {
  if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
    engine.quit();
  }
}

void AppTemplateName::onUpdate(Engine &engine, float delta_seconds) { angle_ += kSpinSpeed * delta_seconds; }

void AppTemplateName::onRender(Engine &engine, renderer::FrameDesc &frame) {
  const Mat4 view = lookAt(kCameraPosition, Vec3(), Vec3(0.0f, 1.0f, 0.0f));
  const Mat4 projection = renderer::perspective(radians(60.0f), engine.aspect(), 0.1f, 100.0f);
  frame.view_projection = projection * view;
  frame.camera_position = kCameraPosition;

  const Vec3 axis(0.3f, 1.0f, 0.2f);
  frame.draws.push_back(
      {.mesh = cube_, .material = material_, .transform = {.rotation = Quat::fromAxisAngle(axis, angle_)}});
}
