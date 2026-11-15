#pragma once

namespace strafe {

class Application;

namespace renderer {
class Renderer;
} // namespace renderer

// What an App can reach of the running engine. Handed to every App callback.
class Engine {
public:
  Engine(const Engine &) = delete;
  Engine &operator=(const Engine &) = delete;

  renderer::Renderer &renderer();

  // Width over height of the render target, for projection matrices. 1 while
  // the window is minimized.
  float aspect() const;

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
