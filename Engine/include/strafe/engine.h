#pragma once

namespace strafe {

class Application;

namespace ecs {
class ComponentRegistry;
} // namespace ecs

namespace reflect {
class TypeRegistry;
} // namespace reflect

// The running engine itself. Usable from App::onInit until App::onShutdown
// returns, like Input and Renderer.
class Engine {
public:
  Engine() = delete;

  // Every [[reflect]] type of the engine and the app, by name or id.
  static const reflect::TypeRegistry &types();
  // Every [[component]] of the engine and the app, by name or id.
  static const ecs::ComponentRegistry &components();

  // Seconds since the engine started.
  static float time();

  // Ends the main loop after the current frame.
  static void quit();

private:
  friend class Application;

  static Application &application();

  // Set by Application while the app runs.
  static Application *application_;
};

} // namespace strafe
