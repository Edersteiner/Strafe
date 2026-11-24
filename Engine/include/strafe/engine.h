#pragma once

namespace strafe {

class Application;

namespace input {
class InputManager;
} // namespace input

namespace reflect {
class TypeRegistry;
} // namespace reflect

namespace renderer {
class Renderer;
} // namespace renderer

// What an App can reach of the running engine. Handed to every App callback.
class Engine {
public:
  Engine(const Engine &) = delete;
  Engine &operator=(const Engine &) = delete;

  renderer::Renderer &renderer();
  input::InputManager &input();
  // Every [[reflect]] type of the engine and the app, by name or id.
  const reflect::TypeRegistry &types() const;

  // Seconds since the engine started.
  float time() const;

  // Ends the main loop after the current frame.
  void quit();

private:
  friend class Application;
  explicit Engine(Application &application) : application_(&application) {}

  Application *application_;
};

} // namespace strafe
