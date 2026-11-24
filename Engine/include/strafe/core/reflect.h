#pragma once

#include <array> // IWYU pragma: keep, for the enum tables in generated headers.
#include <cstddef>
#include <optional>
#include <string_view>
#include <tuple>
#include <type_traits>

namespace strafe::reflect {

// Specialized by the generated .gen.h headers for every [[reflect]] type. It
// has no definition, so an unreflected type simply doesn't satisfy Reflected.
template <typename T> struct TypeInfo;

template <typename T>
concept Reflected = requires { TypeInfo<T>::name; };

template <typename T>
concept ReflectedRecord = Reflected<T> && requires { TypeInfo<T>::fields; };

template <typename T>
concept ReflectedEnum = Reflected<T> && std::is_enum_v<T> && requires { TypeInfo<T>::enumerators; };

template <typename Class, typename Member> struct Field {
  using ClassType = Class;
  using MemberType = Member;

  std::string_view name;
  Member Class::*pointer;

  constexpr Member &get(Class &object) const { return object.*pointer; }
  constexpr const Member &get(const Class &object) const { return object.*pointer; }
};

template <typename Class, typename Member>
constexpr Field<Class, Member> field(std::string_view name, Member Class::*pointer) {
  return {name, pointer};
}

template <typename Enum> struct Enumerator {
  std::string_view name;
  Enum value;
};

template <Reflected T> constexpr std::string_view typeName() { return TypeInfo<T>::name; }

template <ReflectedRecord T> constexpr std::size_t fieldCount() {
  return std::tuple_size_v<std::remove_const_t<decltype(TypeInfo<T>::fields)>>;
}

// Calls visitor once per field in declaration order, with a Field<T, Member>.
template <ReflectedRecord T, typename Visitor> constexpr void forEachField(Visitor &&visitor) {
  std::apply([&](const auto &...fields) { (visitor(fields), ...); }, TypeInfo<T>::fields);
}

template <ReflectedEnum Enum> constexpr std::optional<std::string_view> enumName(Enum value) {
  for (const Enumerator<Enum> &enumerator : TypeInfo<Enum>::enumerators) {
    if (enumerator.value == value) {
      return enumerator.name;
    }
  }
  return std::nullopt;
}

template <ReflectedEnum Enum> constexpr std::optional<Enum> enumFromName(std::string_view name) {
  for (const Enumerator<Enum> &enumerator : TypeInfo<Enum>::enumerators) {
    if (enumerator.name == name) {
      return enumerator.value;
    }
  }
  return std::nullopt;
}

} // namespace strafe::reflect
