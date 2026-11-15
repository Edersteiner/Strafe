#pragma once

#include "strafe/app.h"
#include "strafe/renderer/material.h"
#include "strafe/renderer/texture.h"

class Week3 : public strafe::App {
public:
  bool onInit(strafe::Engine &engine) override;
  void onEvent(strafe::Engine &engine, const SDL_Event &event) override;
  void onUpdate(strafe::Engine &engine, float delta_seconds) override;
  void onRender(strafe::Engine &engine, strafe::renderer::FrameDesc &frame) override;

private:
  strafe::renderer::TextureId redead_texture_;
  strafe::renderer::TextureId grave_texture_;
  strafe::renderer::TextureId link_texture_;
  strafe::renderer::TextureId poe_texture_;
  strafe::renderer::TextureId poe_collector_texture_;

  strafe::renderer::MaterialId redead_material_;
  strafe::renderer::MaterialId grave_material_;
  strafe::renderer::MaterialId link_material_;
  strafe::renderer::MaterialId poe_material_;
  strafe::renderer::MaterialId poe_collector_material_;
  strafe::renderer::MaterialId suzanne_material_;

  bool perspective_ = true;
  // Drives the spin.
  float time_ = 0.0f;
};
