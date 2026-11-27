#pragma once

#include "strafe/core/json.h"
#include "strafe/core/type_registry.h"
#include "strafe/ecs/pool.h"

#include <deque>
#include <memory>
#include <string_view>
#include <unordered_map>

namespace strafe::ecs {

// What a [[component]] can do when only its name or id is known, such as
// attaching it from a file.
struct ComponentDesc {
  std::string_view name;
  reflect::TypeId id;
  std::unique_ptr<PoolBase> (*make_pool)() = nullptr;
  bool (*read)(json::Reader in, void *component) = nullptr;
  void (*write)(json::Writer out, const void *component) = nullptr;
};

// Every [[component]] type, filled in at startup by generated code.
class ComponentRegistry {
public:
  ComponentRegistry() = default;
  ComponentRegistry(const ComponentRegistry &) = delete;
  ComponentRegistry &operator=(const ComponentRegistry &) = delete;

  template <Component T>
    requires reflect::ReflectedRecord<T>
  void add() {
    insert({
        .name = componentName<T>(),
        .id = componentId<T>(),
        .make_pool = []() -> std::unique_ptr<PoolBase> { return std::make_unique<Pool<T>>(); },
        .read = [](json::Reader in, void *component) { return json::read(in, *static_cast<T *>(component)); },
        .write = [](json::Writer out, const void *component) { json::write(out, *static_cast<const T *>(component)); },
    });
  }

  const ComponentDesc *find(std::string_view name) const;
  const ComponentDesc *find(reflect::TypeId id) const;

  // In registration order. Elements never move, so pointers into it stay valid.
  const std::deque<ComponentDesc> &components() const { return components_; }

private:
  void insert(ComponentDesc desc);

  std::deque<ComponentDesc> components_;
  std::unordered_map<uint64_t, const ComponentDesc *> by_id_;
};

} // namespace strafe::ecs

namespace strafe::generated {

// Defined by the codegen for each target, with its [[component]] types.
// Application calls the engine's, then the app's, at startup.
void registerEngineComponents(ecs::ComponentRegistry &registry);
void registerAppComponents(ecs::ComponentRegistry &registry);

} // namespace strafe::generated
