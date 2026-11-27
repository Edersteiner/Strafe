#pragma once

#include <cstdint>
#include <format>

namespace strafe::ecs {

// A slot in a Registry plus the generation of the slot when the entity was
// created. A destroyed entity's slot is reused with the next generation, so an
// old id no longer counts as alive. Entity{} is null.
struct Entity {
  uint32_t index = 0;
  uint32_t generation = 0;

  constexpr bool valid() const { return generation != 0; }
  constexpr bool operator==(const Entity &) const = default;
};

} // namespace strafe::ecs

// prints "index:generation"
template <> struct std::formatter<strafe::ecs::Entity> {
  constexpr auto parse(std::format_parse_context &context) { return context.begin(); }
  auto format(strafe::ecs::Entity entity, std::format_context &context) const {
    return std::format_to(context.out(), "{}:{}", entity.index, entity.generation);
  }
};
