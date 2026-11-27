#include "strafe/ecs/component_registry.h"

#include "strafe/core/log.h"

#include <algorithm>

namespace strafe::ecs {

const ComponentDesc *ComponentRegistry::find(std::string_view name) const {
  const auto found =
      std::ranges::find_if(components_, [&](const ComponentDesc &component) { return component.name == name; });
  return found == components_.end() ? nullptr : &*found;
}

const ComponentDesc *ComponentRegistry::find(reflect::TypeId id) const {
  const auto found = by_id_.find(id.value);
  return found == by_id_.end() ? nullptr : found->second;
}

void ComponentRegistry::insert(ComponentDesc desc) {
  // The TypeRegistry already reports clashing names, so this is the same type
  // registered twice.
  if (by_id_.contains(desc.id.value)) {
    log::warn("The component {} is registered twice", desc.name);
    return;
  }
  const ComponentDesc &added = components_.emplace_back(desc);
  by_id_.emplace(added.id.value, &added);
}

} // namespace strafe::ecs
