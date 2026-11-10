#include "renderer/obj_loader.h"

#include "core/log.h"
#include "core/utils.h"

#include <array>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace strafe::renderer {

namespace {
// Parse string to float
bool tryParseFloat(std::string_view token, float &out) {
  auto [ptr, ec] =
      std::from_chars(token.data(), token.data() + token.size(), out);
  if (ec == std::errc()) {
    return true;
  } else if (ec == std::errc::invalid_argument) {
    log::error("Invalid type: {}", token);
  } else if (ec == std::errc::result_out_of_range) {
    log::error("Not a valid number: {}", token);
  } else {
    log::error("Trailing characters found in: {}", token);
  }
  return false;
}

bool tryParseInt(std::string_view token, int &out) {
  auto [ptr, ec] =
      std::from_chars(token.data(), token.data() + token.size(), out);
  if (ec == std::errc()) {
    return true;
  } else if (ec == std::errc::invalid_argument) {
    log::error("Invalid type: {}", token);
  } else if (ec == std::errc::result_out_of_range) {
    log::error("Not a valid number: {}", token);
  } else {
    log::error("Trailing characters found in: {}", token);
  }
  return false;
}

bool tryParseVec3Field(std::vector<std::string_view> &tokens,
                       std::string_view line, float &x, float &y, float &z) {
  // Extract x, y, z vertex position tokens
  const std::string_view x_tok = tokens[1];
  const std::string_view y_tok = tokens[2];
  const std::string_view z_tok = tokens[3];

  // Parse position tokens to floats
  if (!tryParseFloat(x_tok, x) | !tryParseFloat(y_tok, y) |
      !tryParseFloat(z_tok, z)) {
    log::error("Failed to parse vertex positions: {}", line);
    return false;
  }

  return true;
}

// "1/2/3" -> {"1","2","3"}, "1//3" -> {"1","","3"}, "1" -> {"1","",""}
std::array<std::string_view, 3> splitFaceVertex(std::string_view token) {
  std::array<std::string_view, 3> out{};
  for (size_t i = 0; i < out.size(); ++i) {
    const size_t slash = token.find('/');
    out[i] = token.substr(0, slash);
    if (slash == std::string_view::npos) {
      break;
    }
    token.remove_prefix(slash + 1);
  }
  return out;
}

struct ObjVertex {
  int pos_idx;
  int uv_idx;
  int nrm_idx;

  bool operator==(const ObjVertex &) const = default;
};

struct ObjVertexHash {
  size_t operator()(const ObjVertex &v) const {
    size_t h = std::hash<int>{}(v.pos_idx);
    h = h * 31 + std::hash<int>{}(v.uv_idx);
    h = h * 31 + std::hash<int>{}(v.nrm_idx);
    return h;
  }
};

struct ObjFace {
  std::vector<ObjVertex> verts;
};

} // namespace

std::optional<MeshData> loadFromObj(const char *file_path) {
  // Load file
  std::ifstream obj_file(utils::basePath() + file_path);
  if (!obj_file) {
    log::error("Failed to load: {}", utils::basePath() + file_path);
    return std::nullopt;
  }

  // Working vectors
  std::vector<glm::vec3> vertex_positions;
  std::vector<glm::vec2> vertex_uvs;
  std::vector<glm::vec3> vertex_normals;
  std::vector<ObjFace> faces;

  // Read file line by line
  std::string line;
  while (std::getline(obj_file, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }

    // Split tokens with space as delimiter
    std::vector<std::string_view> tokens = utils::split(line);

    // Ignore blank lines or comments
    if (tokens.empty() || tokens[0].starts_with('#')) {
      continue;
    }

    // The first token will always be the prefix in obj (v, vt, vn, f, etc)
    std::string_view prefix = tokens[0];

    // Parse and store values first, construct mesh vertices and indicies after

    // Vertex - v
    if (prefix == "v") {
      // Expect at minimum 3 position tokens after prefix
      if (tokens.size() < 4) {
        log::error("Vertex does not have 3 positions:\n  ->  {}", line);
        return std::nullopt;
      }

      // Construct, parse and push position into working vertex positions vector
      glm::vec3 pos(0.0f);
      if (!tryParseVec3Field(tokens, line, pos.x, pos.y, pos.z)) {
        return std::nullopt;
      }
      vertex_positions.push_back(pos);

      log::debug("v{} - X: {}, Y: {}, Z: {}", vertex_positions.size(), pos.x,
                 pos.y, pos.z);
    }
    // Vertex Texture Coordinates (UVs) - vt
    if (prefix == "vt") {
      // Expect at minimum 2 position tokens after prefix
      if (tokens.size() < 3) {
        log::error(
            "Vertex does not have complete texture coordinates:\n  ->  {}",
            line);
        return std::nullopt;
      }

      // Extract u, v vertex texture coordinate tokens
      const std::string_view u_tok = tokens[1];
      const std::string_view v_tok = tokens[2];

      // Parse texture coordinates
      glm::vec2 texcoord(0.0f);
      if (!tryParseFloat(u_tok, texcoord.x) |
          !tryParseFloat(v_tok, texcoord.y)) {
        log::error("Failed to parse texture coordinates: {}", line);
        return std::nullopt;
      }
      vertex_uvs.push_back(texcoord);

      log::debug("vt{} - U: {}, V: {}", vertex_uvs.size(), texcoord.x,
                 texcoord.y);
    }

    // Vertex normals - vn
    if (prefix == "vn") {
      // Expect at minimum 3 normal direction tokens after prefix
      if (tokens.size() < 4) {
        log::error("Vertex normal does not have 3 components:\n  ->  {}", line);
        return std::nullopt;
      }

      // Construct, parse and push position into working vertex normal vector
      glm::vec3 dir(0.0f);
      if (!tryParseVec3Field(tokens, line, dir.x, dir.y, dir.z)) {
        return std::nullopt;
      }
      vertex_normals.push_back(dir);

      log::debug("vn{} - X: {}, Y: {}, Z: {}", vertex_normals.size(), dir.x,
                 dir.y, dir.z);
    }

    // Parameter space vertices, ignored for now.
    if (prefix == "vp") {
      continue;
    }

    if (prefix == "f") {
      // Expect at minimum three vertices in face
      // Technically an OBJ can provide two, I might handle this later.
      if (tokens.size() < 4) {
        log::error("Face does not feature at least three vertices:\n  ->  {}",
                   line);
        return std::nullopt;
      }

      ObjFace face{};
      for (size_t i = 1; i < tokens.size(); ++i) {
        std::string_view vertex_token = tokens[i];
        std::array<std::string_view, 3> vertex_attribute_tokens =
            splitFaceVertex(tokens[i]);

        ObjVertex vert{};
        if (!tryParseInt(vertex_attribute_tokens[0], vert.pos_idx) |
            !tryParseInt(vertex_attribute_tokens[1], vert.uv_idx) |
            !tryParseInt(vertex_attribute_tokens[2], vert.nrm_idx)) {
          return std::nullopt;
        }

        // Subtract 1 from indices, OBJ starts indexing at 1, we do 0.
        vert.pos_idx -= 1;
        vert.uv_idx -= 1;
        vert.nrm_idx -= 1;

        face.verts.push_back(vert);
      }
      faces.push_back(face);
      log::debug("f{}:", faces.size());
      for (ObjVertex vert : face.verts) {
        log::debug("  -  pos: {}, uv: {}, nrm {}", vert.pos_idx, vert.uv_idx,
                   vert.nrm_idx);
      }
    }

    // Group, ignored for now.
    if (prefix == "g") {
      continue;
    }

    // Smooth shading toggle, ignored for now
    if (prefix == "s") {
      continue;
    }
  }

  auto inRange = [](int idx, size_t size, bool optional) {
    if (idx < 0) {
      return optional && idx == -1;
    }
    return static_cast<size_t>(idx) < size;
  };

  // Validate every face index before mesh construction to avoid later checks.
  for (size_t f = 0; f < faces.size(); ++f) {
    for (const ObjVertex &v : faces[f].verts) {
      if (!inRange(v.pos_idx, vertex_positions.size(), false) ||
          !inRange(v.uv_idx, vertex_uvs.size(), true) ||
          !inRange(v.nrm_idx, vertex_normals.size(), true)) {
        log::error(
            "Face {} references a missing vertex (pos {}, uv {}, nrm {})",
            f + 1, v.pos_idx, v.uv_idx, v.nrm_idx);
        return std::nullopt;
      }
    }
  }

  // Construct Mesh from parsed data
  MeshData mesh{};

  // Each unique (pos, uv, nrm) triple becomes one GPU vertex,
  // this map remembers which triples were already emitted,
  // so shared corners reuse one index.
  std::unordered_map<ObjVertex, uint32_t, ObjVertexHash> lookup;

  auto emit = [&](const ObjVertex &ov) -> uint32_t {
    if (const auto it = lookup.find(ov); it != lookup.end()) {
      return it->second;
    }

    Vertex v{};
    v.position = vertex_positions[ov.pos_idx];
    v.uv = ov.uv_idx >= 0 ? vertex_uvs[ov.uv_idx] : glm::vec2(0.0f);
    v.normal = ov.nrm_idx >= 0 ? vertex_normals[ov.nrm_idx] : glm::vec3(0.0f);

    const auto index = static_cast<uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back(v);
    lookup.emplace(ov, index);
    return index;
  };

  for (const ObjFace &face : faces) {
    for (size_t i = 1; i + 1 < face.verts.size(); ++i) {
      mesh.indices.push_back(emit(face.verts[0]));
      mesh.indices.push_back(emit(face.verts[i]));
      mesh.indices.push_back(emit(face.verts[i + 1]));
    }
  }

  log::info("Loaded {}: {} vertices, {} triangles", file_path,
            mesh.vertices.size(), mesh.indices.size() / 3);

  return mesh;
}

} // namespace strafe::renderer
