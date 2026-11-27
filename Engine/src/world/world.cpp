#include "strafe/world.h"

#include <cassert>

namespace strafe {

ecs::Registry *World::entities_ = nullptr;

ecs::Registry &World::entities() {
  assert(entities_ && "World is only usable from App::onInit until App::onShutdown returns");
  return *entities_;
}

} // namespace strafe
