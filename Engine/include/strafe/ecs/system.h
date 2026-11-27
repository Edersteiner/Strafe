#pragma once

#include "strafe/ecs/entity.h"
#include "strafe/ecs/pool.h"
#include "strafe/ecs/registry.h"

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <span>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace strafe::ecs {

// When in the frame a system runs.
enum class [[reflect]] Stage : uint8_t {
  // After App::onUpdate.
  Update,
  // Before App::onRender, while the frame's draws are collected.
  Render,
  Count [[skip]],
};

// What a system's parameters are filled in from.
struct SystemContext {
  Registry &registry;
  float delta_seconds = 0.0f;
};

struct SystemDesc {
  std::string_view name;
  std::string_view help;
  // "path:line" of the declaration, for messages.
  std::string_view source;
  Stage stage = Stage::Update;
  // Names of systems in the same stage that this one runs before or after.
  std::vector<std::string_view> before;
  std::vector<std::string_view> after;

  // Filled in by makeSystem.
  void (*run)(SystemContext &context) = nullptr;
  std::vector<std::string_view> reads;
  std::vector<std::string_view> writes;

  bool enabled = true;
};

namespace detail {

template <typename Parameter> inline constexpr bool kIsDelta = std::same_as<Parameter, float>;

template <typename Parameter>
inline constexpr bool kIsEntity = std::same_as<Parameter, Entity> || std::same_as<Parameter, const Entity &>;

template <typename Parameter> inline constexpr bool kIsRegistry = std::same_as<Parameter, Registry &>;

template <typename Parameter>
inline constexpr bool kIsComponent =
    std::is_lvalue_reference_v<Parameter> && std::is_class_v<std::remove_cvref_t<Parameter>> &&
    Component<std::remove_cvref_t<Parameter>> && !kIsEntity<Parameter> && !kIsRegistry<Parameter>;

template <typename Parameter>
inline constexpr bool kIsSystemParameter =
    kIsDelta<Parameter> || kIsEntity<Parameter> || kIsRegistry<Parameter> || kIsComponent<Parameter>;

// The component types among a system's parameters, keeping const, as a tuple.
template <typename Parameter>
using ComponentOf =
    std::conditional_t<kIsComponent<Parameter>, std::tuple<std::remove_reference_t<Parameter>>, std::tuple<>>;
template <typename... Parameters>
using ComponentsOf = decltype(std::tuple_cat(std::declval<ComponentOf<Parameters>>()...));

template <typename Components> struct QueryOver;
template <typename... Components> struct QueryOver<std::tuple<Components...>> {
  static Query<Components...> make(Registry &registry) { return registry.query<Components...>(); }
};

// What the parameter is given: the delta, the registry, or from the query's
// element, the entity or one of its components.
template <typename Parameter, typename Element> decltype(auto) argument(SystemContext &context, Element &element) {
  if constexpr (kIsDelta<Parameter>) {
    return context.delta_seconds;
  } else if constexpr (kIsRegistry<Parameter>) {
    return context.registry;
  } else if constexpr (kIsEntity<Parameter>) {
    return std::get<0>(element);
  } else {
    return std::get<std::remove_reference_t<Parameter> &>(element);
  }
}

template <typename Parameter> void recordAccess(SystemDesc &desc) {
  if constexpr (kIsComponent<Parameter>) {
    const std::string_view name = componentName<std::remove_cvref_t<Parameter>>();
    (std::is_const_v<std::remove_reference_t<Parameter>> ? desc.reads : desc.writes).push_back(name);
  }
}

template <typename Signature> struct SystemOf {
  static_assert(sizeof(Signature) == 0, "A [[system]] must be a function");
};

template <typename Result, typename... Parameters> struct SystemOf<Result (*)(Parameters...)> {
  static_assert(std::is_void_v<Result>, "A [[system]] returns nothing");
  static_assert(
      (kIsSystemParameter<Parameters> && ...),
      "A [[system]] parameter must be a component by reference (const when only read), Entity, "
      "float for the delta seconds, or ecs::Registry &");

  using Components = ComponentsOf<Parameters...>;
  // With components, the system runs once per entity that has them all.
  static constexpr bool kPerEntity = std::tuple_size_v<Components> > 0;
  static_assert(
      kPerEntity || !(kIsEntity<Parameters> || ...),
      "A [[system]] that takes an Entity needs component parameters, which pick the entities");

  template <auto Function> static void run(SystemContext &context) {
    if constexpr (kPerEntity) {
      for (auto element : QueryOver<Components>::make(context.registry)) {
        Function(argument<Parameters>(context, element)...);
      }
    } else {
      std::tuple<> none;
      Function(argument<Parameters>(context, none)...);
    }
  }

  static void recordAccesses(SystemDesc &desc) { (recordAccess<Parameters>(desc), ...); }
};

} // namespace detail

// Completes a generated description from the function's parameter types.
template <auto Function> SystemDesc makeSystem(SystemDesc desc) {
  using System = detail::SystemOf<decltype(Function)>;
  desc.run = &System::template run<Function>;
  System::recordAccesses(desc);
  return desc;
}

// Every [[system]], and the order they run in within each stage.
class Schedule {
public:
  Schedule() = default;
  Schedule(const Schedule &) = delete;
  Schedule &operator=(const Schedule &) = delete;

  // In the default order: engine systems first, then by header and position.
  void add(SystemDesc system);
  // Checks every name and order constraint and sorts each stage. Logs every
  // problem, and returns false if there was one.
  bool finalize();

  void run(Stage stage, Registry &registry, float delta_seconds) const;

  // In the order they run.
  std::span<SystemDesc *const> stage(Stage stage) const { return order_[static_cast<size_t>(stage)]; }
  SystemDesc *find(std::string_view name);

private:
  // Elements never move, so the order can point into it.
  std::deque<SystemDesc> systems_;
  std::array<std::vector<SystemDesc *>, static_cast<size_t>(Stage::Count)> order_;
};

} // namespace strafe::ecs

namespace strafe::generated {

// Defined by the codegen for each target, with its [[system]] functions.
// Application calls the engine's, then the app's, at startup.
void registerEngineSystems(ecs::Schedule &registry);
void registerAppSystems(ecs::Schedule &registry);

} // namespace strafe::generated

#include "strafe/ecs/system.gen.h"
