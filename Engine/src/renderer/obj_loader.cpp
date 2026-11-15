#include "strafe/renderer/obj_loader.h"

#include "strafe/core/log.h"
#include "strafe/core/utils.h"

#include "core/glm_interop.h"

#include "strafe/renderer/mesh.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <fstream>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <glm/glm.hpp>

#include <tiny_obj_loader.h>

namespace strafe::renderer {

namespace {
// Parses the whole token as a number.
template <typename T> bool tryParseNumber(std::string_view token, T &out) {
  const char *end = token.data() + token.size();
  auto [ptr, ec] = std::from_chars(token.data(), end, out);
  if (ec == std::errc::invalid_argument) {
    log::error("Not a number: {}", token);
  } else if (ec == std::errc::result_out_of_range) {
    log::error("Number out of range: {}", token);
  } else if (ptr != end) {
    log::error("Trailing characters found in: {}", token);
  } else {
    return true;
  }
  return false;
}

// Turns an OBJ index into a 0 based one, or -1 for an empty token.
// Negative indices count back from the last element defined so far.
bool tryParseIndex(std::string_view token, size_t defined, bool required, int &out) {
  if (token.empty()) {
    if (required) {
      log::error("Face vertex is missing its position index");
      return false;
    }
    out = -1;
    return true;
  }

  int index = 0;
  if (!tryParseNumber(token, index)) {
    return false;
  }
  if (index > 0) {
    out = index - 1;
    return true;
  }
  if (index < 0 && static_cast<size_t>(-static_cast<int64_t>(index)) <= defined) {
    out = static_cast<int>(defined) + index;
    return true;
  }
  log::error("Invalid face index: {}", token);
  return false;
}

bool tryParseVec3Field(std::vector<std::string_view> &tokens, std::string_view line, float &x, float &y, float &z) {
  // Extract x, y, z vertex position tokens
  const std::string_view x_tok = tokens[1];
  const std::string_view y_tok = tokens[2];
  const std::string_view z_tok = tokens[3];

  // Parse position tokens to floats
  if (!tryParseNumber(x_tok, x) | !tryParseNumber(y_tok, y) | !tryParseNumber(z_tok, z)) {
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

constexpr bool kUseTinyObjLoader = false;
constexpr bool kDebugLog = false;

std::optional<MeshData> loadFromObjCustom(const char *file_path) {
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
    // OBJ allows any whitespace between values,
    // My split funciton only handles a single character, so replace tabs with spaces.
    std::ranges::replace(line, '\t', ' ');

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

      if (kDebugLog) {
        log::debug("v{} - X: {}, Y: {}, Z: {}", vertex_positions.size(), pos.x, pos.y, pos.z);
      }

      continue;
    }
    // Vertex Texture Coordinates (UVs) - vt
    if (prefix == "vt") {
      // Expect at minimum 2 position tokens after prefix
      if (tokens.size() < 3) {
        log::error("Vertex does not have complete texture coordinates:\n  ->  {}", line);
        return std::nullopt;
      }

      // Extract u, v vertex texture coordinate tokens
      const std::string_view u_tok = tokens[1];
      const std::string_view v_tok = tokens[2];

      // Parse texture coordinates
      glm::vec2 texcoord(0.0f);
      if (!tryParseNumber(u_tok, texcoord.x) | !tryParseNumber(v_tok, texcoord.y)) {
        log::error("Failed to parse texture coordinates: {}", line);
        return std::nullopt;
      }
      // Flip texture vertically,
      // OBJ V-Axis points up while Vulkan points down
      texcoord.y = 1 - texcoord.y;
      vertex_uvs.push_back(texcoord);

      if (kDebugLog) {
        log::debug("vt{} - U: {}, V: {}", vertex_uvs.size(), texcoord.x, texcoord.y);
      }

      continue;
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

      if (kDebugLog) {
        log::debug("vn{} - X: {}, Y: {}, Z: {}", vertex_normals.size(), dir.x, dir.y, dir.z);
      }

      continue;
    }

    // Parameter space vertices, ignored for now.
    if (prefix == "vp") {
      continue;
    }

    // Face - f
    if (prefix == "f") {
      // Expect at minimum three vertices in face
      // Technically an OBJ can provide two, I might handle this later.
      if (tokens.size() < 4) {
        log::error("Face does not feature at least three vertices:\n  ->  {}", line);
        return std::nullopt;
      }

      ObjFace face{};
      for (size_t i = 1; i < tokens.size(); ++i) {
        std::string_view vertex_token = tokens[i];
        std::array<std::string_view, 3> vertex_attribute_tokens = splitFaceVertex(tokens[i]);

        ObjVertex vert{};
        // Only the position index is required, the others are optional.
        if (!tryParseIndex(vertex_attribute_tokens[0], vertex_positions.size(), true, vert.pos_idx) |
            !tryParseIndex(vertex_attribute_tokens[1], vertex_uvs.size(), false, vert.uv_idx) |
            !tryParseIndex(vertex_attribute_tokens[2], vertex_normals.size(), false, vert.nrm_idx)) {
          log::error("Failed to parse face vertex: {}", vertex_token);
          return std::nullopt;
        }

        face.verts.push_back(vert);
      }
      faces.push_back(face);

      if (kDebugLog) {
        log::debug("f{}:", faces.size());
        for (ObjVertex vert : face.verts) {
          log::debug("  -  pos: {}, uv: {}, nrm {}", vert.pos_idx, vert.uv_idx, vert.nrm_idx);
        }
      }

      continue;
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

  // Checked against the final counts
  auto in_range = [](int idx, size_t size, bool optional) {
    if (idx < 0) {
      return optional && idx == -1;
    }
    return static_cast<size_t>(idx) < size;
  };

  // Validate every face index before mesh construction to avoid later checks.
  for (size_t f = 0; f < faces.size(); ++f) {
    for (const ObjVertex &v : faces[f].verts) {
      if (!in_range(v.pos_idx, vertex_positions.size(), false) || !in_range(v.uv_idx, vertex_uvs.size(), true) ||
          !in_range(v.nrm_idx, vertex_normals.size(), true)) {
        log::error(
            "Face {} references a missing vertex (pos {}, uv {}, nrm {})", f + 1, v.pos_idx, v.uv_idx, v.nrm_idx);
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

  auto make_vertex = [&](const ObjVertex &ov) {
    Vertex v{};
    v.position = fromGlm(vertex_positions[ov.pos_idx]);
    v.uv = ov.uv_idx >= 0 ? fromGlm(vertex_uvs[ov.uv_idx]) : Vec2();
    v.normal = ov.nrm_idx >= 0 ? fromGlm(vertex_normals[ov.nrm_idx]) : Vec3();
    return v;
  };

  auto emit = [&](const ObjVertex &ov) -> uint32_t {
    if (const auto it = lookup.find(ov); it != lookup.end()) {
      return it->second;
    }

    const auto index = static_cast<uint32_t>(mesh.vertices.size());
    mesh.vertices.push_back(make_vertex(ov));
    lookup.emplace(ov, index);
    return index;
  };

  std::vector<uint32_t> corner_indices;
  for (const ObjFace &face : faces) {
    // A face missing any normal gets one flat normal.
    const bool has_normals = std::ranges::all_of(face.verts, [](const ObjVertex &ov) { return ov.nrm_idx >= 0; });
    corner_indices.clear();
    for (const ObjVertex &ov : face.verts) {
      if (has_normals) {
        corner_indices.push_back(emit(ov));
      } else {
        corner_indices.push_back(static_cast<uint32_t>(mesh.vertices.size()));
        mesh.vertices.push_back(make_vertex(ov));
      }
    }

    // Fan triangulation
    Vec3 face_normal{};
    for (size_t i = 1; i + 1 < corner_indices.size(); ++i) {
      const uint32_t a = corner_indices[0];
      const uint32_t b = corner_indices[i];
      const uint32_t c = corner_indices[i + 1];
      mesh.indices.insert(mesh.indices.end(), {a, b, c});

      face_normal += cross(
          mesh.vertices[b].position - mesh.vertices[a].position, mesh.vertices[c].position - mesh.vertices[a].position);
    }

    if (!has_normals) {
      for (const uint32_t index : corner_indices) {
        mesh.vertices[index].normal = normalize(face_normal);
      }
    }
  }

  if (mesh.indices.empty()) {
    log::error("{} contains no faces", file_path);
    return std::nullopt;
  }

  log::info("Loaded {}: {} vertices, {} triangles", file_path, mesh.vertices.size(), mesh.indices.size() / 3);

  return mesh;
}

std::optional<MeshData> loadFromObjTiny(const char *file_path) {
  tinyobj::attrib_t attrib;

  std::vector<tinyobj::shape_t> shapes;
  // Materials are unused for now.
  std::vector<tinyobj::material_t> materials;

  std::string warn;
  std::string err;

  const std::string path = utils::basePath() + file_path;
  // Looks for .mtl files next to the .obj instead of in the working directory.
  // Not that important currently since we dont handle .mtl yet.
  const std::string directory = path.substr(0, path.find_last_of("/\\") + 1);
  const bool ok = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, path.c_str(), directory.c_str());
  // tinyobjloader repeats a warning for every face it applies to.
  std::unordered_set<std::string_view> seen;
  for (const std::string_view line : utils::split(warn, '\n')) {
    if (seen.insert(line).second) {
      log::warn("tinyobjloader: {}", line);
    }
  }
  if (!ok || !err.empty()) {
    for (const std::string_view line : utils::split(err, '\n')) {
      log::error("tinyobjloader: {}", line);
    }
    log::error("Failed to load: {}", path);
    return std::nullopt;
  }

  const size_t position_count = attrib.vertices.size() / 3;
  const size_t uv_count = attrib.texcoords.size() / 2;
  const size_t normal_count = attrib.normals.size() / 3;

  MeshData mesh{};

  for (const auto &shape : shapes) {
    // LoadObj triangulates by default
    for (size_t i = 0; i + 2 < shape.mesh.indices.size(); i += 3) {
      std::array<Vertex, 3> triangle{};
      bool has_normals = true;

      for (size_t corner = 0; corner < 3; ++corner) {
        const tinyobj::index_t index = shape.mesh.indices[i + corner];
        Vertex &vertex = triangle[corner];

        // Check out of range indicies
        if (index.vertex_index < 0 || static_cast<size_t>(index.vertex_index) >= position_count ||
            index.texcoord_index >= static_cast<int>(uv_count) ||
            index.normal_index >= static_cast<int>(normal_count)) {
          log::error(
              "{} references a missing vertex (pos {}, uv {}, nrm {})", file_path, index.vertex_index,
              index.texcoord_index, index.normal_index);
          return std::nullopt;
        }

        vertex.position = Vec3{
            attrib.vertices[3 * index.vertex_index + 0],
            attrib.vertices[3 * index.vertex_index + 1],
            attrib.vertices[3 * index.vertex_index + 2],
        };

        if (index.texcoord_index >= 0) {
          vertex.uv = Vec2{
              attrib.texcoords[2 * index.texcoord_index + 0],
              1.0f - attrib.texcoords[2 * index.texcoord_index + 1], // Flip V for Vulkan
          };
        }

        if (index.normal_index >= 0) {
          vertex.normal = Vec3{
              attrib.normals[3 * index.normal_index + 0],
              attrib.normals[3 * index.normal_index + 1],
              attrib.normals[3 * index.normal_index + 2],
          };
        } else {
          has_normals = false;
        }
      }

      // Generate normals for faces without one
      if (!has_normals) {
        const Vec3 normal =
            normalize(cross(triangle[1].position - triangle[0].position, triangle[2].position - triangle[0].position));
        for (Vertex &vertex : triangle) {
          vertex.normal = normal;
        }
      }

      for (const Vertex &vertex : triangle) {
        mesh.indices.push_back(static_cast<uint32_t>(mesh.vertices.size()));
        mesh.vertices.push_back(vertex);
      }
    }
  }

  if (mesh.indices.empty()) {
    log::error("{} contains no faces", file_path);
    return std::nullopt;
  }

  log::info("Loaded {}: {} vertices, {} triangles", file_path, mesh.vertices.size(), mesh.indices.size() / 3);

  return mesh;
}

} // namespace

std::optional<MeshData> loadFromObj(const char *file_path) {
  if (kUseTinyObjLoader) {
    log::debug("Loading OBJ with tinyobjloader: {}", file_path);
    return loadFromObjTiny(file_path);
  } else {
    log::debug("Loading OBJ with custom loader: {}", file_path);
    return loadFromObjCustom(file_path);
  }
}

} // namespace strafe::renderer
