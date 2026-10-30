#include "core/application.h"

int main() {
  strafe::Application app;
  if (app.initialize()) {
    app.run();
  }
  app.shutdown();
  return 0;
}
