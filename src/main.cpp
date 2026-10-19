#include <iostream>

#include "core/application.h"

int main(int argc, char **argv) {
  strafe::Application app;
  if (app.initialize()) {
    app.run();
  }
  app.shutdown();
  return 0;
}
