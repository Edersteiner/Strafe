#include "renderer/obj_loader.h"

#include "core/log.h"
#include "core/utils.h"

#include <fstream>
#include <iostream>
#include <string>

#include <glm/glm.hpp>

namespace strafe::renderer {

namespace {
bool tryParseFloat(std::string_view token, float &out) {
  auto [ptr, ec] =
      std::from_chars(token.data(), token.data() + token.size(), out);
  if (ec == std::errc()) {
    return true;
  } else if (ec == std::errc::invalid_argument) {
    log::error("Failed to parse vertex position: {}", token);
  } else if (ec == std::errc::result_out_of_range) {
    log::error("Not a valid number: {}", token);
  } else {
    log::error("Trailing characters found in: {}", token);
  }
  return false;
}
} // namespace

Mesh loadFromObj(const char *file_path) {
  std::ifstream obj_file(utils::basePath() + file_path);
  if (!obj_file) {
    log::error("Failed to load: {}", utils::basePath() + file_path);
  }

  std::vector<glm::vec3> vertex_positions;
  std::vector<glm::vec2> vertex_uvs;
  std::vector<glm::vec3> vertex_normals;

  std::string line;
  while (std::getline(obj_file, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    std::vector<std::string_view> tokens = utils::split(line);
    if (tokens.empty() || tokens[0].starts_with('#')) {
      continue;
    }
    std::string_view prefix = tokens[0];
    // Vertex
    if (prefix == "v") {
      if (tokens.size() < 4) {
        log::error("Vertex does not have 3 positions:\n -> {}", line);
      }
      const std::string_view x_tok = tokens[1];
      const std::string_view y_tok = tokens[2];
      const std::string_view z_tok = tokens[3];

      float x, y, z = 0.0f;

      bool result = true;
      result = tryParseFloat(x_tok, x);
      result = tryParseFloat(y_tok, y);
      result = tryParseFloat(z_tok, z);

      if (!result) {
        log::error("Failed to parse vertex: {}", line);
      }

      glm::vec3 pos(x, y, z);
      vertex_positions.push_back(pos);

      log::debug("v{} - X: {}, Y: {}, Z: {}", vertex_positions.size(), x, y, z);
    }
    if (prefix == "vt") {
      // Texcoord
      log::debug("{} : Texcoord", prefix);
    }
    if (prefix == "vn") {
      // Normals
      log::debug("{} : Normals", prefix);
    }
    if (prefix == "vp") {
      // Parameter space vertices
      log::debug("{} : PS", prefix);
    }
    if (prefix == "f") {
      // Face
      log::debug("{} : Face", prefix);
    }
  }

  return {};
}

} // namespace strafe::renderer
