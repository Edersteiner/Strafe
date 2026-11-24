#include "app_template_file.h"

#include "strafe/core/math.h"
#include "strafe/engine.h"
#include "strafe/input/input_manager.h"
#include "strafe/renderer/camera.h"
#include "strafe/renderer/frame_desc.h"
#include "strafe/renderer/primitives.h"
#include "strafe/renderer/renderer.h"

#include <optional>

using namespace strafe;

namespace {

constexpr Vec3 kCameraPosition(0.0f, 1.5f, 4.0f);
constexpr float kSpinSpeed = 0.8f; // Radians per second.

} // namespace

bool AppTemplateName::onInit(Engine &engine) {
  // Actions are named by the game. One bind lists every input that triggers it.
  engine.input().bind("Quit", {input::Key::Escape});

  renderer::Renderer &renderer = engine.renderer();

  // "lit" is one of the engine's shaders. An app shader with the same name in
  // shaders/ would replace it.
  const std::optional<gfx::ShaderId> lit = renderer.loadShader("lit");
  if (!lit) {
    return false;
  }
  const std::optional<renderer::MaterialId> material = renderer.createMaterial(*lit);
  if (!material) {
    return false;
  }
  material_ = *material;

  // Fields of LitParams in lit.slang, by name. "albedo" is left unset, so it
  // samples the 1x1 white texture.
  renderer.setParam(material_, "tint", Vec4(0.9f, 0.55f, 0.2f, 1.0f));
  return true;
}

void AppTemplateName::onUpdate(Engine &engine, float delta_seconds) {
  if (engine.input().isActionJustPressed("Quit")) {
    engine.quit();
  }
  angle_ += kSpinSpeed * delta_seconds;
}

void AppTemplateName::onRender(Engine &engine, renderer::FrameDesc &frame) {
  frame.camera.transform.position = kCameraPosition;
  frame.camera.lookAt(Vec3());
  frame.camera.projection = renderer::Perspective{.fov_y = radians(60.0f), .far_plane = 100.0f};

  const Vec3 axis(0.3f, 1.0f, 0.2f);
  frame.draws.push_back(
      {.mesh = engine.renderer().primitive(renderer::Primitive::Cube),
       .material = material_,
       .transform = {.rotation = Quat::fromAxisAngle(axis, angle_)}});
}
