#pragma once

#include "strafe/core/math.h"
#include "strafe/core/reflect.h"

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

struct yyjson_val;     // NOLINT(readability-identifier-naming)
struct yyjson_mut_val; // NOLINT(readability-identifier-naming)

namespace strafe::json {

namespace detail {
struct ReadContext;
struct WriteContext;

template <typename Signature> class FunctionRef;
template <typename Result, typename... Args> class FunctionRef<Result(Args...)> {
public:
  template <typename F>
    requires std::invocable<F &, Args...> && (!std::same_as<std::remove_cvref_t<F>, FunctionRef>)
  FunctionRef(F &&function) // NOLINT(misc-explicit-constructor):
      : object_(const_cast<void *>(static_cast<const void *>(&function))),
        call_([](void *object, Args... args) -> Result {
          return (*static_cast<std::remove_reference_t<F> *>(object))(std::forward<Args>(args)...);
        }) {}

  Result operator()(Args... args) const { return call_(object_, std::forward<Args>(args)...); }

private:
  void *object_;
  Result (*call_)(void *, Args...);
};
} // namespace detail

// One value of a document being read. Every read checks the JSON's type and
// logs an error with the file, line and path.
class Reader {
public:
  bool isNull() const;

  bool readBool(bool &value) const;
  bool readInt(int64_t &value, int64_t min, int64_t max) const;
  bool readUint(uint64_t &value, uint64_t max) const;
  bool readNumber(double &value) const;
  // The view lives as long as the document.
  bool readString(std::string_view &value) const;
  bool readString(std::string &value) const;
  // An array of exactly values.size() numbers.
  bool readFloats(std::initializer_list<float *> values) const;

  // The array's size, after checking it is an array of size elements when given.
  std::optional<size_t> arraySize(std::optional<size_t> size = std::nullopt) const;
  // Reads every element, even after one fails, so every error is reported.
  bool forEachElement(detail::FunctionRef<bool(size_t, Reader)> read) const;
  // Fails for anything but an object, with what in the message, like "strafe::Transform".
  bool forEachMember(std::string_view what, detail::FunctionRef<bool(std::string_view, Reader)> read) const;

  void error(std::string_view message) const;
  // Reports "'name' isn't what", suggesting the closest of candidates.
  void unknownName(std::string_view what, std::string_view name, std::span<const std::string_view> candidates) const;

private:
  friend struct detail::ReadContext;
  Reader(detail::ReadContext *context, yyjson_val *value) : context_(context), value_(value) {}

  bool expect(bool matches, std::string_view expected) const;

  detail::ReadContext *context_;
  yyjson_val *value_;
};

// Where one value goes in a document being written: the root, the end of an
// array, or a member of an object.
class Writer {
public:
  void null() const;
  void boolean(bool value) const;
  void integer(int64_t value) const;
  void unsignedInteger(uint64_t value) const;
  void number(float value) const;
  void number(double value) const;
  void string(std::string_view value) const;
  void floats(std::initializer_list<float> values) const;

  // Writes an empty array or object here, and returns where its elements or
  // members go.
  Writer array() const;
  Writer object() const;
  // Where the member called key goes, on a writer returned by object().
  Writer member(std::string_view key) const;

  // Marks the document as failed and logs the message with the path.
  void error(std::string_view message) const;

private:
  friend struct detail::WriteContext;
  Writer(
      detail::WriteContext *context, yyjson_mut_val *container, std::optional<std::string_view> key,
      const Writer *parent)
      : context_(context), container_(container), key_(key), parent_(parent) {}

  void place(yyjson_mut_val *value) const;
  // placed is true when the value this writer adds is already in its container.
  std::string path(bool placed) const;

  detail::WriteContext *context_;
  // Null for the root.
  yyjson_mut_val *container_;
  // Set for a member of an object.
  std::optional<std::string_view> key_;
  // The writer of the enclosing array or object, for error paths.
  const Writer *parent_;
};

// Specialize for a type that isn't written field by field:
//
//   template <> struct json::Serializer<Vec3> {
//     static void write(json::Writer out, const Vec3 &value);
//     static bool read(json::Reader in, Vec3 &value);
//   };
template <typename T> struct Serializer;

template <typename T>
concept CustomSerialized = requires(Writer out, Reader in, const T &input, T &output) {
  Serializer<T>::write(out, input);
  { Serializer<T>::read(in, output) } -> std::same_as<bool>;
};

namespace detail {
template <typename T> inline constexpr bool kIsOptional = false;
template <typename T> inline constexpr bool kIsOptional<std::optional<T>> = true;
template <typename T> inline constexpr bool kIsVector = false;
template <typename T, typename A> inline constexpr bool kIsVector<std::vector<T, A>> = true;
template <typename T> inline constexpr bool kIsArray = false;
template <typename T, size_t N> inline constexpr bool kIsArray<std::array<T, N>> = true;
template <typename T> inline constexpr bool kAlwaysFalse = false;
} // namespace detail

template <typename T> void write(Writer out, const T &value);
template <typename T> bool read(Reader in, T &value);

template <> struct Serializer<Vec2> {
  static void write(Writer out, const Vec2 &value) { out.floats({value.x, value.y}); }
  static bool read(Reader in, Vec2 &value) { return in.readFloats({&value.x, &value.y}); }
};

template <> struct Serializer<Vec3> {
  static void write(Writer out, const Vec3 &value) { out.floats({value.x, value.y, value.z}); }
  static bool read(Reader in, Vec3 &value) { return in.readFloats({&value.x, &value.y, &value.z}); }
};

template <> struct Serializer<Vec4> {
  static void write(Writer out, const Vec4 &value) { out.floats({value.x, value.y, value.z, value.w}); }
  static bool read(Reader in, Vec4 &value) { return in.readFloats({&value.x, &value.y, &value.z, &value.w}); }
};

// [x, y, z, w].
template <> struct Serializer<Quat> {
  static void write(Writer out, const Quat &value) { out.floats({value.x, value.y, value.z, value.w}); }
  static bool read(Reader in, Quat &value) { return in.readFloats({&value.x, &value.y, &value.z, &value.w}); }
};

// Four columns of four numbers.
template <> struct Serializer<Mat4> {
  static void write(Writer out, const Mat4 &value) { json::write(out, value.columns); }
  static bool read(Reader in, Mat4 &value) { return json::read(in, value.columns); }
};

namespace detail {

template <reflect::ReflectedRecord T> bool readRecord(Reader in, T &value) {
  static constexpr auto kNames = reflect::fieldNames<T>();
  std::array<bool, kNames.size()> seen{};
  return in.forEachMember(reflect::typeName<T>(), [&](std::string_view key, Reader member) {
    bool found = false;
    bool ok = true;
    size_t index = 0;
    reflect::forEachField<T>([&](const auto &field) {
      if (!found && field.name == key) {
        found = true;
        if (seen[index]) {
          member.error(std::string("field '").append(key).append("' appears twice"));
          ok = false;
        } else {
          seen[index] = true;
          ok = read(member, field.get(value));
        }
      }
      ++index;
    });
    if (!found) {
      member.unknownName(std::string("a field of ").append(reflect::typeName<T>()), key, kNames);
      return false;
    }
    return ok;
  });
}

template <typename T> bool readElements(Reader in, T &elements) {
  return in.forEachElement([&](size_t index, Reader element) { return read(element, elements[index]); });
}

} // namespace detail

// Writes value as JSON. Math types are arrays, enums are names, and
// [[reflect]] records are objects with every field.
template <typename T> void write(Writer out, const T &value) {
  if constexpr (CustomSerialized<T>) {
    Serializer<T>::write(out, value);
  } else if constexpr (std::same_as<T, bool>) {
    out.boolean(value);
  } else if constexpr (std::integral<T> && std::is_signed_v<T>) {
    out.integer(value);
  } else if constexpr (std::integral<T>) {
    out.unsignedInteger(value);
  } else if constexpr (std::floating_point<T>) {
    out.number(value);
  } else if constexpr (std::same_as<T, std::string>) {
    out.string(value);
  } else if constexpr (reflect::ReflectedEnum<T>) {
    if (const std::optional<std::string_view> name = reflect::enumName(value)) {
      out.string(*name);
    } else {
      out.error(
          std::string(reflect::typeName<T>())
              .append(" has no name for the value ")
              .append(std::to_string(static_cast<int64_t>(value))));
      out.null();
    }
  } else if constexpr (detail::kIsOptional<T>) {
    if (value) {
      write(out, *value);
    } else {
      out.null();
    }
  } else if constexpr (detail::kIsVector<T> || detail::kIsArray<T>) {
    const Writer elements = out.array();
    for (const auto &element : value) {
      write(elements, element);
    }
  } else if constexpr (reflect::ReflectedRecord<T>) {
    const Writer members = out.object();
    reflect::forEachField<T>([&](const auto &field) { write(members.member(field.name), field.get(value)); });
  } else {
    static_assert(
        detail::kAlwaysFalse<T>,
        "This type can't be written as JSON. Make it [[reflect]] or specialize "
        "json::Serializer for it.");
  }
}

// Reads value from JSON, the inverse of write. Missing fields keep their
// current values, and unknown fields are errors.
template <typename T> bool read(Reader in, T &value) {
  if constexpr (CustomSerialized<T>) {
    return Serializer<T>::read(in, value);
  } else if constexpr (std::same_as<T, bool>) {
    return in.readBool(value);
  } else if constexpr (std::integral<T> && std::is_signed_v<T>) {
    int64_t number = 0;
    if (!in.readInt(number, std::numeric_limits<T>::min(), std::numeric_limits<T>::max())) {
      return false;
    }
    value = static_cast<T>(number);
    return true;
  } else if constexpr (std::integral<T>) {
    uint64_t number = 0;
    if (!in.readUint(number, std::numeric_limits<T>::max())) {
      return false;
    }
    value = static_cast<T>(number);
    return true;
  } else if constexpr (std::floating_point<T>) {
    double number = 0.0;
    if (!in.readNumber(number)) {
      return false;
    }
    value = static_cast<T>(number);
    return true;
  } else if constexpr (std::same_as<T, std::string>) {
    return in.readString(value);
  } else if constexpr (reflect::ReflectedEnum<T>) {
    std::string_view name;
    if (!in.readString(name)) {
      return false;
    }
    if (const std::optional<T> parsed = reflect::enumFromName<T>(name)) {
      value = *parsed;
      return true;
    }
    static constexpr auto kNames = reflect::enumNames<T>();
    in.unknownName(std::string("a name in ").append(reflect::typeName<T>()), name, kNames);
    return false;
  } else if constexpr (detail::kIsOptional<T>) {
    if (in.isNull()) {
      value.reset();
      return true;
    }
    if (!value) {
      value.emplace();
    }
    return read(in, *value);
  } else if constexpr (detail::kIsVector<T>) {
    const std::optional<size_t> size = in.arraySize();
    if (!size) {
      return false;
    }
    value.clear();
    value.resize(*size);
    return detail::readElements(in, value);
  } else if constexpr (detail::kIsArray<T>) {
    return in.arraySize(std::tuple_size_v<T>) && detail::readElements(in, value);
  } else if constexpr (reflect::ReflectedRecord<T>) {
    return detail::readRecord(in, value);
  } else {
    static_assert(
        detail::kAlwaysFalse<T>,
        "This type can't be read from JSON. Make it [[reflect]] or specialize "
        "json::Serializer for it.");
  }
}

namespace detail {

// A parsed document. Syntax errors are logged by parse.
class InputDocument {
public:
  // source names the text in messages, usually its path.
  explicit InputDocument(std::string_view source);
  ~InputDocument();
  InputDocument(const InputDocument &) = delete;
  InputDocument &operator=(const InputDocument &) = delete;

  bool parse(std::string_view text);
  Reader root();

private:
  std::unique_ptr<ReadContext> context_;
};

class OutputDocument {
public:
  explicit OutputDocument(std::string_view source);
  ~OutputDocument();
  OutputDocument(const OutputDocument &) = delete;
  OutputDocument &operator=(const OutputDocument &) = delete;

  Writer root();
  // Pretty printed, or nothing when a write failed.
  std::optional<std::string> finish();

private:
  std::unique_ptr<WriteContext> context_;
};

bool hasExtension(std::string_view path, std::string_view extension, std::string_view type_name);
std::optional<std::string> readText(std::string_view path);
bool writeText(std::string_view path, std::string_view text);

template <typename T> std::optional<std::string> toString(const T &value, std::string_view source) {
  OutputDocument document(source);
  write(document.root(), value);
  return document.finish();
}

} // namespace detail

// Parses text as a T, starting from a default constructed one. Logs every
// problem and returns nothing if there was one.
template <typename T> std::optional<T> fromString(std::string_view text, std::string_view source = "<string>") {
  detail::InputDocument document(source);
  if (!document.parse(text)) {
    return std::nullopt;
  }
  T value{};
  if (!read(document.root(), value)) {
    return std::nullopt;
  }
  return value;
}

template <typename T> std::optional<std::string> toString(const T &value) {
  return detail::toString(value, "<string>");
}

// Reads a [[file]] type from a file with its extension, like a .scene.
template <reflect::FileType T> std::optional<T> loadFile(std::string_view path) {
  if (!detail::hasExtension(path, reflect::TypeInfo<T>::file_extension, reflect::typeName<T>())) {
    return std::nullopt;
  }
  const std::optional<std::string> text = detail::readText(path);
  return text ? fromString<T>(*text, path) : std::nullopt;
}

template <reflect::FileType T> bool saveFile(std::string_view path, const T &value) {
  if (!detail::hasExtension(path, reflect::TypeInfo<T>::file_extension, reflect::typeName<T>())) {
    return false;
  }
  const std::optional<std::string> text = detail::toString(value, path);
  return text && detail::writeText(path, *text);
}

} // namespace strafe::json
