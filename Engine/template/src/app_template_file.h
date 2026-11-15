#pragma once

#include "strafe/app.h"
#include "strafe/renderer/material.h"
#include "strafe/renderer/mesh.h"

// Made by `xmake new-app`. Draws a spinning lit cube to show the app is wired
// up. Replace it with your own scene.
//
// Assets go in assets/ and shaders in shaders/ next to this app's xmake.lua.
// Both are copied next to the exe on every build, and paths are relative to
// the exe, e.g. "assets/models/thing.obj".
class AppTemplateName : public strafe::App {
public:
  bool onInit(strafe::Engine &engine) override;
  void onEvent(strafe::Engine &engine, const SDL_Event &event) override;
  void onUpdate(strafe::Engine &engine, float delta_seconds) override;
  void onRender(strafe::Engine &engine, strafe::renderer::FrameDesc &frame) override;

private:
  strafe::renderer::MeshId cube_;
  strafe::renderer::MaterialId material_;
  float angle_ = 0.0f;
};
