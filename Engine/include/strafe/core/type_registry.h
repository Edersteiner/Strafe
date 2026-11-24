#pragma once

#include "strafe/core/math.h"
#include "strafe/core/reflect.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace strafe::reflect {

// Types that aren't [[reflect]] but are common enough as fields that runtime
// code needs to recognize them. Specialized below with each type's name.
template <typename T> struct BuiltinType;

#define STRAFE_BUILTIN_TYPE(Type, Name)                                                                                \
  template <> struct BuiltinType<Type> {                                                                               \
    static constexpr std::string_view name = Name;                                                                     \
  }

STRAFE_BUILTIN_TYPE(bool, "bool");
STRAFE_BUILTIN_TYPE(int8_t, "int8");
STRAFE_BUILTIN_TYPE(uint8_t, "uint8");
STRAFE_BUILTIN_TYPE(int16_t, "int16");
STRAFE_BUILTIN_TYPE(uint16_t, "uint16");
STRAFE_BUILTIN_TYPE(int32_t, "int32");
STRAFE_BUILTIN_TYPE(uint32_t, "uint32");
STRAFE_BUILTIN_TYPE(int64_t, "int64");
STRAFE_BUILTIN_TYPE(uint64_t, "uint64");
STRAFE_BUILTIN_TYPE(float, "float");
STRAFE_BUILTIN_TYPE(double, "double");
STRAFE_BUILTIN_TYPE(std::string, "std::string");
STRAFE_BUILTIN_TYPE(Vec2, "strafe::Vec2");
STRAFE_BUILTIN_TYPE(Vec3, "strafe::Vec3");
STRAFE_BUILTIN_TYPE(Vec4, "strafe::Vec4");
STRAFE_BUILTIN_TYPE(Quat, "strafe::Quat");
STRAFE_BUILTIN_TYPE(Mat4, "strafe::Mat4");

#undef STRAFE_BUILTIN_TYPE

template <typename T>
concept Builtin = requires { BuiltinType<T>::name; };

// Identifies a type by a hash of its qualified name. It is the same in
// every target and on every platform.
struct TypeId {
  uint64_t value = 0;

  constexpr bool valid() const { return value != 0; }
  constexpr bool operator==(const TypeId &) const = default;
};

constexpr TypeId hashTypeName(std::string_view name) {
  uint64_t hash = 0xcbf29ce484222325ULL;
  for (const char c : name) {
    hash ^= static_cast<unsigned char>(c);
    hash *= 0x100000001b3ULL;
  }
  return {hash};
}

// Invalid for types that are neither reflected nor builtin.
template <typename T> constexpr TypeId typeId() {
  if constexpr (Reflected<T>) {
    return hashTypeName(TypeInfo<T>::name);
  } else if constexpr (Builtin<T>) {
    return hashTypeName(BuiltinType<T>::name);
  } else {
    return {};
  }
}

enum class TypeKind : uint8_t { Builtin, Record, Enum };

struct FieldDesc {
  std::string_view name;
  // Invalid when the field's type is neither reflected nor builtin.
  TypeId type;
  // The field inside an object of the owning type.
  void *(*address)(void *object) = nullptr;
};

struct EnumeratorDesc {
  std::string_view name;
  int64_t value = 0;
};

// Everything known about a type at runtime, for code that only has a name or
// an id, such as an inspector or a file loader.
struct TypeDesc {
  std::string_view name;
  TypeId id;
  TypeKind kind = TypeKind::Builtin;
  size_t size = 0;
  size_t alignment = 0;
  std::vector<FieldDesc> fields;
  std::vector<EnumeratorDesc> enumerators;

  const FieldDesc *findField(std::string_view field_name) const;
};

class TypeRegistry {
public:
  TypeRegistry() = default;
  TypeRegistry(const TypeRegistry &) = delete;
  TypeRegistry &operator=(const TypeRegistry &) = delete;

  template <typename T>
    requires Reflected<T> || Builtin<T>
  void add();

  // Adds every BuiltinType above.
  void addBuiltins();

  const TypeDesc *find(std::string_view name) const;
  const TypeDesc *find(TypeId id) const;
  template <typename T> const TypeDesc *find() const { return find(typeId<T>()); }

  // In registration order. Elements never move, so pointers into it stay valid.
  const std::deque<TypeDesc> &types() const { return types_; }

  // True once a registration clashed, which the registry has already logged.
  bool failed() const { return failed_; }

private:
  // Lets the name index be searched with a string_view.
  struct NameHash {
    using is_transparent = void; // NOLINT(readability-identifier-naming): the standard library's name.
    size_t operator()(std::string_view name) const { return std::hash<std::string_view>{}(name); }
  };

  template <typename T, size_t Index> static FieldDesc fieldDesc();
  void insert(TypeDesc desc);

  std::deque<TypeDesc> types_;
  std::unordered_map<std::string, const TypeDesc *, NameHash, std::equal_to<>> by_name_;
  std::unordered_map<uint64_t, const TypeDesc *> by_id_;
  bool failed_ = false;
};

template <typename T, size_t Index> FieldDesc TypeRegistry::fieldDesc() {
  using FieldType = std::remove_cvref_t<decltype(std::get<Index>(TypeInfo<T>::fields))>;
  return {
      .name = std::get<Index>(TypeInfo<T>::fields).name,
      .type = typeId<typename FieldType::MemberType>(),
      .address = [](void *object) -> void * {
        return &(static_cast<T *>(object)->*std::get<Index>(TypeInfo<T>::fields).pointer);
      }};
}

template <typename T>
  requires Reflected<T> || Builtin<T>
void TypeRegistry::add() {
  TypeDesc desc{.id = typeId<T>(), .size = sizeof(T), .alignment = alignof(T)};
  if constexpr (ReflectedRecord<T>) {
    desc.name = TypeInfo<T>::name;
    desc.kind = TypeKind::Record;
    [&]<size_t... Indices>(std::index_sequence<Indices...>) {
      (desc.fields.push_back(fieldDesc<T, Indices>()), ...);
    }(std::make_index_sequence<fieldCount<T>()>());
  } else if constexpr (ReflectedEnum<T>) {
    desc.name = TypeInfo<T>::name;
    desc.kind = TypeKind::Enum;
    for (const Enumerator<T> &enumerator : TypeInfo<T>::enumerators) {
      desc.enumerators.push_back({.name = enumerator.name, .value = static_cast<int64_t>(enumerator.value)});
    }
  } else {
    desc.name = BuiltinType<T>::name;
  }
  insert(std::move(desc));
}

} // namespace strafe::reflect

namespace strafe::generated {

// Defined by the codegen for each target. Application calls the engine's,
// then the app's, at startup. Every app defines registerAppTypes, empty when
// it has nothing reflected.
void registerEngineTypes(reflect::TypeRegistry &registry);
void registerAppTypes(reflect::TypeRegistry &registry);

} // namespace strafe::generated
