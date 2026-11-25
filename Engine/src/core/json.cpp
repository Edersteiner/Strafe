#include "strafe/core/json.h"

#include "strafe/core/log.h"

#include <yyjson.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>

namespace strafe::json {

namespace detail {

struct ReadContext {
  // Where the reader is, from the root, for messages.
  struct Segment {
    std::optional<std::string_view> key;
    size_t index = 0;
    // Into buffer, or null when the value has no text of its own to point at.
    const char *position = nullptr;
  };

  std::string source;
  // Parsed in place, so keys and strings point into it and give line numbers.
  std::string buffer;
  // Taken before parsing, which turns escapes like \n into the characters
  // they stand for and so changes what counting the buffer's newlines gives.
  std::vector<size_t> line_starts;
  yyjson_doc *document = nullptr;
  std::vector<Segment> path;

  Reader reader(yyjson_val *value) { return {this, value}; }

  std::string pathText() const {
    std::string text;
    for (const Segment &segment : path) {
      if (segment.key) {
        if (!text.empty()) {
          text += '.';
        }
        text += *segment.key;
      } else {
        text += std::format("[{}]", segment.index);
      }
    }
    return text;
  }

  // "file:line:column", or just the file for a position outside the text.
  std::string locate(const char *position) const {
    if (position == nullptr || position < buffer.data() || position >= buffer.data() + buffer.size()) {
      return source;
    }
    const auto offset = static_cast<size_t>(position - buffer.data());
    const auto next_line = std::ranges::upper_bound(line_starts, offset);
    const auto line = static_cast<size_t>(next_line - line_starts.begin());
    return std::format("{}:{}:{}", source, line, offset - *std::prev(next_line) + 1);
  }

  void error(const char *position, std::string_view message) const {
    for (auto it = path.rbegin(); position == nullptr && it != path.rend(); ++it) {
      position = it->position;
    }
    const std::string where = pathText();
    if (where.empty()) {
      log::error("{}: {}", locate(position), message);
    } else {
      log::error("{}: {}: {}", locate(position), where, message);
    }
  }
};

struct WriteContext {
  std::string source;
  yyjson_mut_doc *document = nullptr;
  bool failed = false;

  Writer root() { return {this, nullptr, std::nullopt, nullptr}; }
};

namespace {

// The opening quote of a string value or key in the parsed buffer.
const char *stringPosition(yyjson_val *value) { return yyjson_is_str(value) ? yyjson_get_str(value) - 1 : nullptr; }

std::string describe(yyjson_val *value) {
  switch (yyjson_get_type(value)) {
  case YYJSON_TYPE_NULL:
    return "null";
  case YYJSON_TYPE_BOOL:
    return yyjson_get_bool(value) ? "true" : "false";
  case YYJSON_TYPE_NUM:
    if (yyjson_is_uint(value)) {
      return std::format("{}", yyjson_get_uint(value));
    }
    if (yyjson_is_sint(value)) {
      return std::format("{}", yyjson_get_sint(value));
    }
    return std::format("{}", yyjson_get_real(value));
  case YYJSON_TYPE_STR:
    return std::format("\"{}\"", std::string_view(yyjson_get_str(value), yyjson_get_len(value)));
  case YYJSON_TYPE_ARR:
    return std::format("an array of {}", yyjson_arr_size(value));
  case YYJSON_TYPE_OBJ:
    return "an object";
  default:
    return "nothing";
  }
}

std::string lowercase(std::string_view text) {
  std::string result(text);
  std::ranges::transform(result, result.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return result;
}

size_t editDistance(std::string_view a, std::string_view b) {
  std::vector<size_t> row(b.size() + 1);
  for (size_t j = 0; j <= b.size(); ++j) {
    row[j] = j;
  }
  for (size_t i = 1; i <= a.size(); ++i) {
    size_t diagonal = row[0];
    row[0] = i;
    for (size_t j = 1; j <= b.size(); ++j) {
      const size_t above = row[j];
      row[j] = std::min({row[j] + 1, row[j - 1] + 1, diagonal + (a[i - 1] == b[j - 1] ? 0 : 1)});
      diagonal = above;
    }
  }
  return row[b.size()];
}

// Close enough to be a typo of name: one edit, or a third of its length.
std::optional<std::string_view> closest(std::string_view name, std::span<const std::string_view> candidates) {
  const std::string lowered = lowercase(name);
  const size_t limit = std::max<size_t>(1, name.size() / 3);
  std::optional<std::string_view> best;
  size_t best_distance = limit + 1;
  for (const std::string_view candidate : candidates) {
    const size_t distance = editDistance(lowered, lowercase(candidate));
    if (distance < best_distance) {
      best = candidate;
      best_distance = distance;
    }
  }
  return best;
}

void appendScalar(std::string &out, const yyjson_mut_val *value) {
  size_t length = 0;
  char *text = yyjson_mut_val_write(value, YYJSON_WRITE_NOFLAG, &length);
  if (text != nullptr) {
    out.append(text, length);
    std::free(text); // NOLINT(cppcoreguidelines-no-malloc): yyjson allocates with malloc.
  }
}

bool isContainer(yyjson_mut_val *value) { return yyjson_mut_is_arr(value) || yyjson_mut_is_obj(value); }

// Two space indents, with arrays of plain values on one line so vectors stay
// readable.
// NOLINTNEXTLINE(misc-no-recursion): bounded by the depth of the written value.
void print(std::string &out, yyjson_mut_val *value, size_t depth) {
  const std::string indent((depth + 1) * 2, ' ');
  const std::string closing_indent(depth * 2, ' ');
  if (yyjson_mut_is_obj(value)) {
    if (yyjson_mut_obj_size(value) == 0) {
      out += "{}";
      return;
    }
    out += "{\n";
    yyjson_mut_obj_iter it = yyjson_mut_obj_iter_with(value);
    size_t remaining = yyjson_mut_obj_size(value);
    while (yyjson_mut_val *key = yyjson_mut_obj_iter_next(&it)) {
      out += indent;
      appendScalar(out, key);
      out += ": ";
      print(out, yyjson_mut_obj_iter_get_val(key), depth + 1);
      out += --remaining > 0 ? ",\n" : "\n";
    }
    out += closing_indent + "}";
  } else if (yyjson_mut_is_arr(value)) {
    if (yyjson_mut_arr_size(value) == 0) {
      out += "[]";
      return;
    }
    bool flat = true;
    yyjson_mut_arr_iter check = yyjson_mut_arr_iter_with(value);
    while (yyjson_mut_val *element = yyjson_mut_arr_iter_next(&check)) {
      flat = flat && !isContainer(element);
    }
    out += flat ? "[" : "[\n";
    yyjson_mut_arr_iter it = yyjson_mut_arr_iter_with(value);
    size_t remaining = yyjson_mut_arr_size(value);
    while (yyjson_mut_val *element = yyjson_mut_arr_iter_next(&it)) {
      if (!flat) {
        out += indent;
      }
      print(out, element, depth + 1);
      if (--remaining > 0) {
        out += flat ? ", " : ",\n";
      } else if (!flat) {
        out += '\n';
      }
    }
    out += flat ? "]" : closing_indent + "]";
  } else {
    appendScalar(out, value);
  }
}

} // namespace

InputDocument::InputDocument(std::string_view source) : context_(std::make_unique<ReadContext>()) {
  context_->source = source;
}

InputDocument::~InputDocument() { yyjson_doc_free(context_->document); }

bool InputDocument::parse(std::string_view text) {
  context_->line_starts = {0};
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '\n') {
      context_->line_starts.push_back(i + 1);
    }
  }
  // Reading in place needs padding past the end.
  context_->buffer.assign(text);
  context_->buffer.append(YYJSON_PADDING_SIZE, '\0');
  yyjson_read_err error{};
  context_->document = yyjson_read_opts(context_->buffer.data(), text.size(), YYJSON_READ_INSITU, nullptr, &error);
  if (context_->document == nullptr) {
    log::error("{}: {}", context_->locate(context_->buffer.data() + error.pos), error.msg);
    return false;
  }
  return true;
}

Reader InputDocument::root() { return context_->reader(yyjson_doc_get_root(context_->document)); }

OutputDocument::OutputDocument(std::string_view source) : context_(std::make_unique<WriteContext>()) {
  context_->source = source;
  context_->document = yyjson_mut_doc_new(nullptr);
}

OutputDocument::~OutputDocument() { yyjson_mut_doc_free(context_->document); }

Writer OutputDocument::root() { return context_->root(); }

std::optional<std::string> OutputDocument::finish() {
  yyjson_mut_val *root = yyjson_mut_doc_get_root(context_->document);
  if (context_->failed || root == nullptr) {
    if (root == nullptr) {
      log::error("{}: nothing was written", context_->source);
    }
    return std::nullopt;
  }
  std::string text;
  print(text, root, 0);
  text += '\n';
  return text;
}

bool hasExtension(std::string_view path, std::string_view extension, std::string_view type_name) {
  if (std::filesystem::path(path).extension().string() == std::format(".{}", extension)) {
    return true;
  }
  log::error("{}: a {} file must end in .{}", path, type_name, extension);
  return false;
}

std::optional<std::string> readText(std::string_view path) {
  std::ifstream file{std::filesystem::path(path), std::ios::binary};
  if (!file) {
    log::error("{}: can't open the file", path);
    return std::nullopt;
  }
  return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

bool writeText(std::string_view path, std::string_view text) {
  std::ofstream file{std::filesystem::path(path), std::ios::binary | std::ios::trunc};
  if (!file.write(text.data(), static_cast<std::streamsize>(text.size()))) {
    log::error("{}: can't write the file", path);
    return false;
  }
  return true;
}

} // namespace detail

bool Reader::isNull() const { return yyjson_is_null(value_); }

void Reader::error(std::string_view message) const { context_->error(detail::stringPosition(value_), message); }

bool Reader::expect(bool matches, std::string_view expected) const {
  if (!matches) {
    error(std::format("expected {}, found {}", expected, detail::describe(value_)));
  }
  return matches;
}

bool Reader::readBool(bool &value) const {
  if (!expect(yyjson_is_bool(value_), "true or false")) {
    return false;
  }
  value = yyjson_get_bool(value_);
  return true;
}

bool Reader::readInt(int64_t &value, int64_t min, int64_t max) const {
  const bool fits = yyjson_is_sint(value_)
      ? yyjson_get_sint(value_) >= min && yyjson_get_sint(value_) <= max
      : yyjson_is_uint(value_) && yyjson_get_uint(value_) <= static_cast<uint64_t>(max);
  if (!expect(fits, std::format("an integer from {} to {}", min, max))) {
    return false;
  }
  value = yyjson_is_sint(value_) ? yyjson_get_sint(value_) : static_cast<int64_t>(yyjson_get_uint(value_));
  return true;
}

bool Reader::readUint(uint64_t &value, uint64_t max) const {
  if (!expect(yyjson_is_uint(value_) && yyjson_get_uint(value_) <= max, std::format("an integer from 0 to {}", max))) {
    return false;
  }
  value = yyjson_get_uint(value_);
  return true;
}

bool Reader::readNumber(double &value) const {
  if (!expect(yyjson_is_num(value_), "a number")) {
    return false;
  }
  value = yyjson_get_num(value_);
  return true;
}

bool Reader::readString(std::string_view &value) const {
  if (!expect(yyjson_is_str(value_), "a string")) {
    return false;
  }
  value = std::string_view(yyjson_get_str(value_), yyjson_get_len(value_));
  return true;
}

bool Reader::readString(std::string &value) const {
  std::string_view view;
  if (!readString(view)) {
    return false;
  }
  value = view;
  return true;
}

bool Reader::readFloats(std::initializer_list<float *> values) const {
  const std::string expected = std::format("an array of {} numbers", values.size());
  if (!expect(yyjson_is_arr(value_) && yyjson_arr_size(value_) == values.size(), expected)) {
    return false;
  }
  bool ok = true;
  size_t index = 0;
  for (float *value : values) {
    yyjson_val *element = yyjson_arr_get(value_, index);
    if (yyjson_is_num(element)) {
      *value = static_cast<float>(yyjson_get_num(element));
    } else {
      context_->path.push_back({.index = index, .position = detail::stringPosition(element)});
      ok = context_->reader(element).expect(false, "a number") && ok;
      context_->path.pop_back();
    }
    ++index;
  }
  return ok;
}

std::optional<size_t> Reader::arraySize(std::optional<size_t> size) const {
  const bool matches = yyjson_is_arr(value_) && (!size || yyjson_arr_size(value_) == *size);
  if (!expect(matches, size ? std::format("an array of {}", *size) : std::string("an array"))) {
    return std::nullopt;
  }
  return yyjson_arr_size(value_);
}

bool Reader::forEachElement(detail::FunctionRef<bool(size_t, Reader)> read) const {
  if (!expect(yyjson_is_arr(value_), "an array")) {
    return false;
  }
  bool ok = true;
  size_t index = 0;
  yyjson_arr_iter it = yyjson_arr_iter_with(value_);
  while (yyjson_val *element = yyjson_arr_iter_next(&it)) {
    context_->path.push_back({.index = index, .position = detail::stringPosition(element)});
    ok = read(index, context_->reader(element)) && ok;
    context_->path.pop_back();
    ++index;
  }
  return ok;
}

bool Reader::forEachMember(std::string_view what, detail::FunctionRef<bool(std::string_view, Reader)> read) const {
  if (!expect(yyjson_is_obj(value_), std::format("an object for {}", what))) {
    return false;
  }
  bool ok = true;
  yyjson_obj_iter it = yyjson_obj_iter_with(value_);
  while (yyjson_val *key = yyjson_obj_iter_next(&it)) {
    const std::string_view name(yyjson_get_str(key), yyjson_get_len(key));
    context_->path.push_back({.key = name, .position = detail::stringPosition(key)});
    ok = read(name, context_->reader(yyjson_obj_iter_get_val(key))) && ok;
    context_->path.pop_back();
  }
  return ok;
}

void Reader::unknownName(
    std::string_view what, std::string_view name, std::span<const std::string_view> candidates) const {
  std::string message = std::format("'{}' isn't {}", name, what);
  if (const std::optional<std::string_view> suggestion = detail::closest(name, candidates)) {
    message += std::format("; did you mean '{}'?", *suggestion);
  } else if (!candidates.empty()) {
    message += "; expected one of ";
    for (size_t i = 0; i < candidates.size(); ++i) {
      message += std::format("{}{}", i > 0 ? ", " : "", candidates[i]);
    }
  }
  // At the key for an unknown field, which is the innermost segment.
  const bool is_key = !context_->path.empty() && context_->path.back().key == name;
  context_->error(is_key ? context_->path.back().position : detail::stringPosition(value_), message);
}

void Writer::place(yyjson_mut_val *value) const {
  yyjson_mut_doc *document = context_->document;
  if (container_ == nullptr) {
    yyjson_mut_doc_set_root(document, value);
  } else if (yyjson_mut_is_arr(container_)) {
    yyjson_mut_arr_append(container_, value);
  } else if (key_) {
    yyjson_mut_obj_add(container_, yyjson_mut_strncpy(document, key_->data(), key_->size()), value);
  } else {
    error("values in an object need a key, so write them through member()");
  }
}

void Writer::null() const { place(yyjson_mut_null(context_->document)); }

void Writer::boolean(bool value) const { place(yyjson_mut_bool(context_->document, value)); }

void Writer::integer(int64_t value) const { place(yyjson_mut_sint(context_->document, value)); }

void Writer::unsignedInteger(uint64_t value) const { place(yyjson_mut_uint(context_->document, value)); }

void Writer::number(float value) const {
  if (!std::isfinite(value)) {
    error(std::format("{} can't be written as JSON", value));
    null();
    return;
  }
  place(yyjson_mut_float(context_->document, value));
}

void Writer::number(double value) const {
  if (!std::isfinite(value)) {
    error(std::format("{} can't be written as JSON", value));
    null();
    return;
  }
  place(yyjson_mut_real(context_->document, value));
}

void Writer::string(std::string_view value) const {
  place(yyjson_mut_strncpy(context_->document, value.data(), value.size()));
}

void Writer::floats(std::initializer_list<float> values) const {
  const Writer elements = array();
  for (const float value : values) {
    elements.number(value);
  }
}

Writer Writer::array() const {
  yyjson_mut_val *value = yyjson_mut_arr(context_->document);
  place(value);
  return {context_, value, std::nullopt, this};
}

Writer Writer::object() const {
  yyjson_mut_val *value = yyjson_mut_obj(context_->document);
  place(value);
  return {context_, value, std::nullopt, this};
}

Writer Writer::member(std::string_view key) const {
  if (container_ == nullptr || !yyjson_mut_is_obj(container_) || key_) {
    error("member() needs the writer object() returned");
  }
  return {context_, container_, key, this};
}

// NOLINTNEXTLINE(misc-no-recursion): walks up the writers of the enclosing values.
std::string Writer::path(bool placed) const {
  if (parent_ == nullptr) {
    return "";
  }
  // A child's container is already in this array, so its index is the last.
  std::string base = parent_->path(true);
  if (yyjson_mut_is_arr(container_)) {
    return std::format("{}[{}]", base, yyjson_mut_arr_size(container_) - (placed ? 1 : 0));
  }
  if (key_) {
    return base.empty() ? std::string(*key_) : std::format("{}.{}", base, *key_);
  }
  return base;
}

void Writer::error(std::string_view message) const {
  context_->failed = true;
  const std::string where = path(false);
  if (where.empty()) {
    log::error("{}: {}", context_->source, message);
  } else {
    log::error("{}: {}: {}", context_->source, where, message);
  }
}

} // namespace strafe::json
