#pragma once

#include "strafe/app.h"
#include "strafe/renderer/material.h"
#include "strafe/renderer/mesh.h"
#include "strafe/renderer/texture.h"

class Week3 : public strafe::App {
public:
  bool onInit(strafe::Engine &engine) override;
  void onEvent(strafe::Engine &engine, const SDL_Event &event) override;
  void onUpdate(strafe::Engine &engine, float delta_seconds) override;
  void onRender(strafe::Engine &engine, strafe::renderer::FrameDesc &frame) override;

private:
  strafe::renderer::MeshId redead_;
  strafe::renderer::TextureId redead_texture_;
  strafe::renderer::MaterialId redead_material_;
  strafe::renderer::MaterialId ghost_material_;
  strafe::renderer::MaterialId unlit_material_;
  strafe::renderer::MaterialId normals_material_;

  bool perspective_ = true;
  // Drives the spin.
  float time_ = 0.0f;
};
