#pragma once

#include "strafe/core/type_registry.h"
#include "strafe/ecs/entity.h"

#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace strafe::ecs {

// What a type needs to be stored as a component.
template <typename T>
concept Component = std::default_initializable<T> && std::movable<T> && std::same_as<T, std::remove_cvref_t<T>>;

namespace detail {

// The type's name as the compiler spells it, for types without a reflected one.
template <typename T> constexpr std::string_view compilerTypeName() {
#if defined(_MSC_VER) && !defined(__clang__)
  constexpr std::string_view kSignature = __FUNCSIG__;
  constexpr std::string_view kPrefix = "compilerTypeName<";
  constexpr std::string_view kSuffix = ">(void)";
#else
  constexpr std::string_view kSignature = __PRETTY_FUNCTION__;
  constexpr std::string_view kPrefix = "T = ";
  constexpr std::string_view kSuffix = (kSignature.find(';') != std::string_view::npos) ? ";" : "]";
#endif
  constexpr size_t kStart = kSignature.find(kPrefix);
  if constexpr (kStart == std::string_view::npos) {
    return kSignature;
  } else {
    constexpr std::string_view kRest = kSignature.substr(kStart + kPrefix.size());
    return kRest.substr(0, kRest.find(kSuffix));
  }
}

} // namespace detail

// The qualified name for a reflected type, otherwise the compiler's spelling.
template <Component T> constexpr std::string_view componentName() {
  if constexpr (reflect::Reflected<T>) {
    return reflect::TypeInfo<T>::name;
  } else if constexpr (reflect::Builtin<T>) {
    return reflect::BuiltinType<T>::name;
  } else {
    return detail::compilerTypeName<T>();
  }
}

// The reflected TypeId when there is one. Otherwise only stable within one
// build, which is fine for components that are never saved.
template <Component T> constexpr reflect::TypeId componentId() { return reflect::hashTypeName(componentName<T>()); }

// The entities with one component type, as a sparse set: sparse_ maps an
// entity's index to its position in dense_, and Pool<T> keeps the components
// in the same order as dense_. Removing swaps the last element into the gap.
class PoolBase {
public:
  PoolBase(reflect::TypeId id, std::string_view name) : id_(id), name_(name) {}
  virtual ~PoolBase() = default;
  PoolBase(const PoolBase &) = delete;
  PoolBase &operator=(const PoolBase &) = delete;

  reflect::TypeId id() const { return id_; }
  std::string_view name() const { return name_; }

  size_t size() const { return dense_.size(); }
  bool contains(Entity entity) const { return find(entity) != kAbsent; }
  // In storage order, which changes when components are removed.
  std::span<const Entity> entities() const { return dense_; }

  // Type erased access, for code that only knows the component by its id.
  // Null when the entity doesn't have the component.
  virtual void *get(Entity entity) = 0;
  // The entity's component, default constructed when it had none.
  virtual void *getOrAdd(Entity entity) = 0;
  virtual bool remove(Entity entity) = 0;
  virtual void clear() = 0;

protected:
  static constexpr uint32_t kAbsent = UINT32_MAX;

  uint32_t find(Entity entity) const {
    if (entity.index >= sparse_.size()) {
      return kAbsent;
    }
    const uint32_t position = sparse_[entity.index];
    return position != kAbsent && dense_[position] == entity ? position : kAbsent;
  }

  // Returns the new entity's position, which is the end.
  uint32_t insert(Entity entity) {
    assert(!iterating() && "Adding a component of a type that a query is iterating over");
    if (entity.index >= sparse_.size()) {
      sparse_.resize(entity.index + 1, kAbsent);
    }
    const auto position = static_cast<uint32_t>(dense_.size());
    sparse_[entity.index] = position;
    dense_.push_back(entity);
    return position;
  }

  // Moves the last entity into position. Pool<T> does the same to its components.
  void erase(uint32_t position) {
    const Entity removed = dense_[position];
    const Entity last = dense_.back();
    dense_[position] = last;
    sparse_[last.index] = position;
    sparse_[removed.index] = kAbsent;
    dense_.pop_back();
  }

  void clearEntities() {
    sparse_.clear();
    dense_.clear();
  }

private:
  template <typename... Components> friend class Query;

  bool iterating() const { return queries_ > 0; }

  reflect::TypeId id_;
  std::string_view name_;
  std::vector<uint32_t> sparse_;
  std::vector<Entity> dense_;
  // How many queries over this pool are alive.
  uint32_t queries_ = 0;
};

template <Component T> class Pool final : public PoolBase {
public:
  Pool() : PoolBase(componentId<T>(), componentName<T>()) {}

  // Replaces the component when the entity already has one.
  T &add(Entity entity, T value) {
    if (const uint32_t position = find(entity); position != kAbsent) {
      components_[position] = std::move(value);
      return components_[position];
    }
    insert(entity);
    return components_.emplace_back(std::move(value));
  }

  T *tryGet(Entity entity) {
    const uint32_t position = find(entity);
    return position == kAbsent ? nullptr : &components_[position];
  }
  const T *tryGet(Entity entity) const {
    const uint32_t position = find(entity);
    return position == kAbsent ? nullptr : &components_[position];
  }

  void *get(Entity entity) override { return tryGet(entity); }

  void *getOrAdd(Entity entity) override {
    if (T *existing = tryGet(entity)) {
      return existing;
    }
    return &add(entity, T{});
  }

  bool remove(Entity entity) override {
    const uint32_t position = find(entity);
    if (position == kAbsent) {
      return false;
    }
    if (position + 1 != components_.size()) {
      components_[position] = std::move(components_.back());
    }
    components_.pop_back();
    erase(position);
    return true;
  }

  void clear() override {
    components_.clear();
    clearEntities();
  }

private:
  std::vector<T> components_;
};

} // namespace strafe::ecs
