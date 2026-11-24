#include "app_template_file.h"

#include "strafe/core/math.h"
#include "strafe/engine.h"
#include "strafe/input.h"
#include "strafe/renderer.h"
#include "strafe/renderer/camera.h"

#include <optional>

using namespace strafe;

namespace {

constexpr Vec3 kCameraPosition(0.0f, 1.5f, 4.0f);
constexpr float kSpinSpeed = 0.8f; // Radians per second.

} // namespace

bool AppTemplateName::onInit() {
  // Actions are named by the game. One bind lists every input that triggers it.
  Input::bind("Quit", {input::Key::Escape});

  // "lit" is one of the engine's shaders. An app shader with the same name in
  // shaders/ would replace it.
  const std::optional<gfx::ShaderId> lit = Renderer::loadShader("lit");
  if (!lit) {
    return false;
  }
  const std::optional<renderer::MaterialId> material = Renderer::createMaterial(*lit);
  if (!material) {
    return false;
  }
  material_ = *material;

  // Fields of LitParams in lit.slang, by name. "albedo" is left unset, so it
  // samples the 1x1 white texture.
  Renderer::setParam(material_, "tint", Vec4(0.9f, 0.55f, 0.2f, 1.0f));
  return true;
}

void AppTemplateName::onUpdate(float delta_seconds) {
  if (Input::isActionJustPressed("Quit")) {
    Engine::quit();
  }
  angle_ += kSpinSpeed * delta_seconds;
}

void AppTemplateName::onRender() {
  renderer::FrameDesc &frame = Renderer::frame();
  frame.camera.transform.position = kCameraPosition;
  frame.camera.lookAt(Vec3());
  frame.camera.projection = renderer::Perspective{.fov_y = radians(60.0f), .far_plane = 100.0f};

  const Vec3 axis(0.3f, 1.0f, 0.2f);
  Renderer::draw(
      {.mesh = Renderer::primitive(renderer::Primitive::Cube),
       .material = material_,
       .transform = {.rotation = Quat::fromAxisAngle(axis, angle_)}});
}
