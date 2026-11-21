#pragma once

#include "fly_camera.h"

#include "strafe/app.h"
#include "strafe/renderer/material.h"
#include "strafe/renderer/mesh.h"

// Week 4: a first person flying camera and splitscreen.
//
// Assets go in assets/ and shaders in shaders/ next to this app's xmake.lua.
// Both are copied next to the exe on every build, and paths are relative to
// the exe, e.g. "assets/models/thing.obj".
class Week4 : public strafe::App {
public:
  bool onInit(strafe::Engine &engine) override;
  void onUpdate(strafe::Engine &engine, float delta_seconds) override;
  void onRender(strafe::Engine &engine, strafe::renderer::FrameDesc &frame) override;

private:
  void setMouseCaptured(strafe::Engine &engine, bool captured);

  strafe::renderer::MeshId cube_;
  strafe::renderer::MaterialId material_;

  FlyCamera camera_;
  bool mouse_captured_ = false;
};
