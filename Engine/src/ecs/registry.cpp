#include "strafe/ecs/registry.h"

#include "strafe/ecs/component_registry.h"
#include "strafe/ecs/components.h"

#include <cassert>
#include <memory>
#include <utility>
#include <vector>

namespace strafe::ecs {

Entity Registry::create() {
  uint32_t index = 0;
  if (free_slots_.empty()) {
    index = static_cast<uint32_t>(slots_.size());
    slots_.emplace_back();
  } else {
    index = free_slots_.back();
    free_slots_.pop_back();
  }
  Slot &slot = slots_[index];
  slot.alive = true;
  ++alive_count_;
  return {.index = index, .generation = slot.generation};
}

void Registry::destroy(Entity entity) {
  if (!alive(entity)) {
    return;
  }
  for (const std::unique_ptr<PoolBase> &pool : pools_) {
    pool->remove(entity);
  }
  Slot &slot = slots_[entity.index];
  slot.alive = false;
  // Zero is the null generation, so a wrapped slot skips it.
  slot.generation = slot.generation == UINT32_MAX ? 1 : slot.generation + 1;
  free_slots_.push_back(entity.index);
  --alive_count_;
}

bool Registry::alive(Entity entity) const {
  return entity.index < slots_.size() && slots_[entity.index].alive &&
      slots_[entity.index].generation == entity.generation;
}

Entity Registry::atIndex(uint32_t index) const {
  if (index >= slots_.size() || !slots_[index].alive) {
    return {};
  }
  return {.index = index, .generation = slots_[index].generation};
}

std::vector<Entity> Registry::entities() const {
  std::vector<Entity> alive_entities;
  alive_entities.reserve(alive_count_);
  for (uint32_t index = 0; index < slots_.size(); ++index) {
    if (slots_[index].alive) {
      alive_entities.push_back({.index = index, .generation = slots_[index].generation});
    }
  }
  return alive_entities;
}

void Registry::clear() {
  for (const Entity entity : entities()) {
    destroy(entity);
  }
}

Entity Registry::find(std::string_view name) {
  const PoolBase *names = findPool(componentId<Name>());
  if (!names) {
    return {};
  }
  // Pool order changes as components are removed, so the lowest slot wins,
  // which keeps the answer stable.
  Entity found;
  for (const Entity entity : names->entities()) {
    if (tryGet<Name>(entity)->value == name && (!found.valid() || entity.index < found.index)) {
      found = entity;
    }
  }
  return found;
}

PoolBase *Registry::findPool(reflect::TypeId id) {
  const auto found = pools_by_id_.find(id.value);
  return found == pools_by_id_.end() ? nullptr : found->second;
}

const PoolBase *Registry::findPool(reflect::TypeId id) const {
  const auto found = pools_by_id_.find(id.value);
  return found == pools_by_id_.end() ? nullptr : found->second;
}

PoolBase &Registry::pool(const ComponentDesc &component) {
  if (PoolBase *found = findPool(component.id)) {
    return *found;
  }
  return addPool(component.make_pool());
}

PoolBase &Registry::addPool(std::unique_ptr<PoolBase> pool) {
  PoolBase &added = *pool;
  pools_by_id_.emplace(added.id().value, &added);
  pools_.push_back(std::move(pool));
  return added;
}

} // namespace strafe::ecs
