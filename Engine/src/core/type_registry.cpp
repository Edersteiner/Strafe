#include "strafe/core/type_registry.h"

#include "strafe/core/log.h"

#include <algorithm>

namespace strafe::reflect {

const FieldDesc *TypeDesc::findField(std::string_view field_name) const {
  const auto it = std::ranges::find(fields, field_name, &FieldDesc::name);
  return it != fields.end() ? &*it : nullptr;
}

void TypeRegistry::addBuiltins() {
  add<bool>();
  add<int8_t>();
  add<uint8_t>();
  add<int16_t>();
  add<uint16_t>();
  add<int32_t>();
  add<uint32_t>();
  add<int64_t>();
  add<uint64_t>();
  add<float>();
  add<double>();
  add<std::string>();
  add<Vec2>();
  add<Vec3>();
  add<Vec4>();
  add<Quat>();
  add<Mat4>();
}

const TypeDesc *TypeRegistry::find(std::string_view name) const {
  const auto it = by_name_.find(name);
  return it != by_name_.end() ? it->second : nullptr;
}

const TypeDesc *TypeRegistry::find(TypeId id) const {
  const auto it = by_id_.find(id.value);
  return it != by_id_.end() ? it->second : nullptr;
}

void TypeRegistry::insert(TypeDesc desc) {
  if (const TypeDesc *existing = find(desc.name)) {
    log::error("Type '{}' is registered twice", existing->name);
    failed_ = true;
    return;
  }
  if (const TypeDesc *existing = find(desc.id)) {
    log::error("Types '{}' and '{}' have the same id, so one of them needs another name", existing->name, desc.name);
    failed_ = true;
    return;
  }
  const TypeDesc &stored = types_.emplace_back(std::move(desc));
  by_name_.emplace(std::string(stored.name), &stored);
  by_id_.emplace(stored.id.value, &stored);
}

} // namespace strafe::reflect
