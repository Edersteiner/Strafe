#include "strafe/scene/scene.h"

#include "strafe/core/log.h"
#include "strafe/core/utils.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <format>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace strafe::scene {

namespace {

// Fields that may follow an entry's fixed arguments, in any order.
constexpr uint8_t kPosition = 1 << 0;
constexpr uint8_t kRotation = 1 << 1;
constexpr uint8_t kScale = 1 << 2;
constexpr uint8_t kColor = 1 << 3;

bool parseFloat(std::string_view token, float &out) {
  const char *end = token.data() + token.size();
  const auto [ptr, ec] = std::from_chars(token.data(), end, out);
  return ec == std::errc() && ptr == end;
}

// Reads one line's tokens in order and reports errors with the line number.
class LineReader {
public:
  LineReader(std::string_view path, int line_number) : path_(path), line_number_(line_number) {}

  // Splits on whitespace, keeps quoted strings whole and stops at '#'.
  bool tokenize(std::string_view line) {
    size_t i = 0;
    while (i < line.size()) {
      if (std::isspace(static_cast<unsigned char>(line[i]))) {
        ++i;
      } else if (line[i] == '#') {
        break;
      } else if (line[i] == '"') {
        const size_t close = line.find('"', i + 1);
        if (close == std::string_view::npos) {
          return fail("Unterminated quote");
        }
        tokens_.push_back(line.substr(i + 1, close - i - 1));
        i = close + 1;
      } else {
        size_t end = i;
        while (end < line.size() && !std::isspace(static_cast<unsigned char>(line[end])) && line[end] != '#') {
          ++end;
        }
        tokens_.push_back(line.substr(i, end - i));
        i = end;
      }
    }
    return true;
  }

  bool empty() const { return tokens_.empty(); }
  bool done() const { return next_ >= tokens_.size(); }

  bool fail(std::string_view message) const {
    log::error("{}:{}: {}", path_, line_number_, message);
    return false;
  }

  bool readWord(std::string_view what, std::string_view &out) {
    if (done()) {
      return fail(std::format("Missing {}", what));
    }
    out = tokens_[next_++];
    return true;
  }

  bool readFloat(std::string_view what, float &out) {
    std::string_view token;
    if (!readWord(what, token)) {
      return false;
    }
    if (!parseFloat(token, out)) {
      return fail(std::format("Expected a number for {}, got '{}'", what, token));
    }
    return true;
  }

  bool readVec3(std::string_view what, Vec3 &out) {
    return readFloat(what, out.x) && readFloat(what, out.y) && readFloat(what, out.z);
  }

  // True if the token after the next `offset` ones is a number.
  bool peekFloat(size_t offset = 0) const {
    float ignored = 0.0f;
    return next_ + offset < tokens_.size() && parseFloat(tokens_[next_ + offset], ignored);
  }

  // Reads "pos x y z", "rot x y z" in degrees, "scale s" or "scale x y z",
  // and "color r g b" until the line ends.
  bool readFields(std::string_view entry, uint8_t allowed, Transform *transform, Vec4 *color) {
    while (!done()) {
      const std::string_view field = tokens_[next_++];
      uint8_t flag = 0;
      if (field == "pos") {
        flag = kPosition;
      } else if (field == "rot") {
        flag = kRotation;
      } else if (field == "scale") {
        flag = kScale;
      } else if (field == "color") {
        flag = kColor;
      } else {
        return fail(std::format("Unknown field '{}'", field));
      }
      if ((allowed & flag) == 0) {
        return fail(std::format("A {} has no '{}'", entry, field));
      }

      bool ok = true;
      if (flag == kPosition) {
        ok = readVec3("pos", transform->position);
      } else if (flag == kRotation) {
        Vec3 degrees;
        ok = readVec3("rot", degrees);
        transform->rotation = Quat::fromEuler({radians(degrees.x), radians(degrees.y), radians(degrees.z)});
      } else if (flag == kScale) {
        // Three numbers in a row scale each axis, one scales them all.
        if (peekFloat(0) && peekFloat(1) && peekFloat(2)) {
          ok = readVec3("scale", transform->scale);
        } else {
          float uniform = 1.0f;
          ok = readFloat("scale", uniform);
          transform->scale = Vec3(uniform);
        }
      } else {
        Vec3 rgb;
        ok = readVec3("color", rgb);
        *color = Vec4(rgb, 1.0f);
      }
      if (!ok) {
        return false;
      }
    }
    return true;
  }

private:
  std::string_view path_;
  int line_number_;
  std::vector<std::string_view> tokens_;
  size_t next_ = 0;
};

bool parseLine(LineReader &reader, Scene &scene) {
  std::string_view keyword;
  if (!reader.readWord("keyword", keyword)) {
    return false;
  }

  if (keyword == "name") {
    std::string_view name;
    if (!reader.readWord("scene name", name)) {
      return false;
    }
    if (!reader.done()) {
      return reader.fail("A name with spaces needs quotes");
    }
    scene.name = name;
    return true;
  }

  if (keyword == "brush") {
    Brush brush;
    if (!reader.readVec3("brush min corner", brush.min) || !reader.readVec3("brush max corner", brush.max) ||
        !reader.readFields("brush", kColor, nullptr, &brush.color)) {
      return false;
    }
    if (brush.min.x >= brush.max.x || brush.min.y >= brush.max.y || brush.min.z >= brush.max.z) {
      return reader.fail("A brush's min corner must be below its max corner on every axis");
    }
    scene.brushes.push_back(brush);
    return true;
  }

  if (keyword == "marker") {
    Marker marker;
    std::string_view name;
    if (!reader.readWord("marker name", name) ||
        !reader.readFields("marker", kPosition | kRotation | kScale, &marker.transform, nullptr)) {
      return false;
    }
    marker.name = name;
    scene.markers.push_back(std::move(marker));
    return true;
  }

  if (keyword == "object") {
    Object object;
    std::string_view name;
    std::string_view model_path;
    if (!reader.readWord("object name", name) || !reader.readWord("model path", model_path) ||
        !reader.readFields("object", kPosition | kRotation | kScale | kColor, &object.transform, &object.color)) {
      return false;
    }
    object.name = name;
    object.model_path = model_path;
    scene.objects.push_back(std::move(object));
    return true;
  }

  return reader.fail(std::format("Unknown keyword '{}'", keyword));
}

} // namespace

const Marker *Scene::findMarker(std::string_view marker_name) const {
  const auto it = std::ranges::find(markers, marker_name, &Marker::name);
  return it != markers.end() ? &*it : nullptr;
}

std::vector<const Marker *> Scene::findMarkers(std::string_view marker_name) const {
  std::vector<const Marker *> found;
  for (const Marker &marker : markers) {
    if (marker.name == marker_name) {
      found.push_back(&marker);
    }
  }
  return found;
}

std::optional<Scene> loadScene(std::string_view path) {
  const std::string full_path = utils::basePath() + std::string(path);
  std::ifstream file(full_path);
  if (!file) {
    log::error("Failed to open scene: {}", full_path);
    return std::nullopt;
  }

  Scene scene;
  std::string line;
  int line_number = 0;
  while (std::getline(file, line)) {
    ++line_number;
    LineReader reader(path, line_number);
    if (!reader.tokenize(line)) {
      return std::nullopt;
    }
    if (!reader.empty() && !parseLine(reader, scene)) {
      return std::nullopt;
    }
  }

  if (scene.name.empty()) {
    scene.name = path;
  }
  log::info(
      "Loaded scene '{}' from {}: {} brushes, {} markers, {} objects", scene.name, path, scene.brushes.size(),
      scene.markers.size(), scene.objects.size());
  return scene;
}

} // namespace strafe::scene
