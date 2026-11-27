#pragma once

#include "strafe/ecs/entity.h"
#include "strafe/ecs/pool.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <span>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace strafe::ecs {

struct ComponentDesc;
class Registry;

// Every entity that has all of Components, as a range of tuples:
//
//   for (auto [entity, transform, player] : registry.query<Transform, const Player>()) { ... }
//
// A const component is only read. The loop goes backwards through the smallest
// pool, so destroying the current entity or removing its components is safe.
// Adding a component of an iterated type is not, and asserts.
template <typename... Components> class Query {
  static_assert(sizeof...(Components) > 0, "A query needs at least one component");

public:
  using Value = std::tuple<Entity, Components &...>;

  explicit Query(Registry &registry);
  ~Query() {
    std::apply([](auto *...pools) { (--pools->queries_, ...); }, pools_);
  }
  Query(const Query &) = delete;
  Query &operator=(const Query &) = delete;
  Query(Query &&) = delete;
  Query &operator=(Query &&) = delete;

  class Iterator {
  public:
    Value operator*() const {
      const Entity entity = query_->driver_->entities()[remaining_ - 1];
      return std::apply([&](auto *...pools) { return Value(entity, *pools->tryGet(entity)...); }, query_->pools_);
    }
    Iterator &operator++() {
      // The pool may have shrunk by the current entity.
      remaining_ = std::min(remaining_ - 1, query_->driver_->size());
      skipMismatches();
      return *this;
    }
    bool operator==(std::default_sentinel_t /* end */) const { return remaining_ == 0; }

  private:
    friend class Query;

    explicit Iterator(const Query &query) : query_(&query), remaining_(query.driver_->size()) { skipMismatches(); }

    void skipMismatches() {
      while (remaining_ > 0 && !query_->matches(query_->driver_->entities()[remaining_ - 1])) {
        --remaining_;
      }
    }

    const Query *query_;
    size_t remaining_;
  };

  Iterator begin() const { return Iterator(*this); }
  std::default_sentinel_t end() const { return {}; }

private:
  bool matches(Entity entity) const {
    return std::apply([&](const auto *...pools) { return (pools->contains(entity) && ...); }, pools_);
  }

  std::tuple<Pool<std::remove_const_t<Components>> *...> pools_;
  // The smallest pool, which the loop walks.
  const PoolBase *driver_ = nullptr;
};

// Owns entities and their components, and nothing else.
class Registry {
public:
  Registry() = default;
  Registry(const Registry &) = delete;
  Registry &operator=(const Registry &) = delete;

  Entity create();
  // Removes every component of the entity. Does nothing for one that isn't alive.
  void destroy(Entity entity);
  bool alive(Entity entity) const;
  // The alive entity in slot index, or null.
  Entity atIndex(uint32_t index) const;
  // How many entities are alive.
  size_t size() const { return alive_count_; }
  // Every alive entity, in slot order.
  std::vector<Entity> entities() const;
  // Destroys every entity. Ids from before stay invalid.
  void clear();

  // Adds or replaces the entity's T.
  //   registry.add<Transform>(entity, {.position = {0.0f, 1.0f, 0.0f}});
  template <Component T> T &add(Entity entity, T value = {}) {
    assert(alive(entity) && "Adding a component to an entity that isn't alive");
    return pool<T>().add(entity, std::move(value));
  }
  template <Component T> bool remove(Entity entity) {
    PoolBase *found = findPool(componentId<T>());
    return found && found->remove(entity);
  }
  template <Component T> bool has(Entity entity) const {
    const PoolBase *found = findPool(componentId<T>());
    return found && found->contains(entity);
  }
  // Null when the entity doesn't have a T.
  template <Component T> T *tryGet(Entity entity) {
    PoolBase *found = findPool(componentId<T>());
    return found ? static_cast<Pool<T> *>(found)->tryGet(entity) : nullptr;
  }
  template <Component T> const T *tryGet(Entity entity) const {
    const PoolBase *found = findPool(componentId<T>());
    return found ? static_cast<const Pool<T> *>(found)->tryGet(entity) : nullptr;
  }
  template <Component T> T &get(Entity entity) {
    T *component = tryGet<T>(entity);
    assert(component && "The entity doesn't have this component");
    return *component;
  }
  template <Component T> const T &get(Entity entity) const {
    const T *component = tryGet<T>(entity);
    assert(component && "The entity doesn't have this component");
    return *component;
  }

  template <typename... Components> Query<Components...> query() { return Query<Components...>(*this); }

  // The first alive entity whose Name is name, or null.
  Entity find(std::string_view name);

  // Type erased access for files and tools, which know components by name.
  // Null when no entity ever had the component.
  PoolBase *findPool(reflect::TypeId id);
  const PoolBase *findPool(reflect::TypeId id) const;
  PoolBase &pool(const ComponentDesc &component);
  // Every pool, in the order they were created.
  std::span<const std::unique_ptr<PoolBase>> pools() const { return pools_; }

  template <Component T> Pool<T> &pool() {
    if (PoolBase *found = findPool(componentId<T>())) {
      assert(found->name() == componentName<T>() && "Two component types have the same id");
      return *static_cast<Pool<T> *>(found);
    }
    return static_cast<Pool<T> &>(addPool(std::make_unique<Pool<T>>()));
  }

private:
  struct Slot {
    uint32_t generation = 1;
    bool alive = false;
  };

  PoolBase &addPool(std::unique_ptr<PoolBase> pool);

  std::vector<Slot> slots_;
  std::vector<uint32_t> free_slots_;
  size_t alive_count_ = 0;
  std::vector<std::unique_ptr<PoolBase>> pools_;
  std::unordered_map<uint64_t, PoolBase *> pools_by_id_;
};

template <typename... Components>
Query<Components...>::Query(Registry &registry) : pools_(&registry.pool<std::remove_const_t<Components>>()...) {
  std::apply(
      [&](auto *...pools) {
        (++pools->queries_, ...);
        driver_ = std::min({static_cast<const PoolBase *>(pools)...}, [](const PoolBase *a, const PoolBase *b) {
          return a->size() < b->size();
        });
      },
      pools_);
}

} // namespace strafe::ecs
