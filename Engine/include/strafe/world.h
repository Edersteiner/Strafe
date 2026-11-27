#pragma once

namespace strafe {

class Application;

namespace ecs {
class Registry;
} // namespace ecs

// What the game takes place in. Usable from App::onInit until App::onShutdown
// returns, like Engine, Input and Renderer.
class World {
public:
  World() = delete;

  // The entities and their components.
  static ecs::Registry &entities();

private:
  friend class Application;

  // Set by Application while the app runs.
  static ecs::Registry *entities_;
};

} // namespace strafe
