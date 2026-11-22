#pragma once

#include "fly_camera.h"

#include "strafe/app.h"
#include "strafe/renderer/material.h"
#include "strafe/scene/scene.h"
#include "strafe/scene/scene_renderer.h"

#include <array>
#include <cstddef>
#include <cstdint>

// Week 4: fly through three scenes to their goals, alone or in splitscreen.
class Week4 : public strafe::App {
public:
  bool onInit(strafe::Engine &engine) override;
  void onUpdate(strafe::Engine &engine, float delta_seconds) override;
  void onRender(strafe::Engine &engine, strafe::renderer::FrameDesc &frame) override;

private:
  static constexpr uint8_t kPlayerCount = 2;

  // Moves every player to the scene's starts. Keeps the current scene on failure.
  bool loadScene(size_t index);
  void setMouseCaptured(strafe::Engine &engine, bool captured);

  strafe::scene::Scene scene_;
  size_t scene_index_ = 0;
  strafe::scene::SceneRenderer scene_renderer_;

  std::array<strafe::renderer::MaterialId, kPlayerCount> player_materials_;
  strafe::renderer::MaterialId goal_material_;

  std::array<FlyCamera, kPlayerCount> cameras_;
  bool mouse_captured_ = false;
};
