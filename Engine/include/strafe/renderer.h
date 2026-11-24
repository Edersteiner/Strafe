#pragma once

#include "strafe/core/types/transform.h"
#include "strafe/gfx/pipeline_manager.h"
#include "strafe/renderer/frame_desc.h"
#include "strafe/renderer/material.h"
#include "strafe/renderer/mesh.h"
#include "strafe/renderer/primitives.h"
#include "strafe/renderer/texture.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace strafe {
class Application;
} // namespace strafe

namespace strafe::renderer {

class Renderer;

enum class DebugView : std::int8_t { None, Wireframe, Normals, DrawId, Count };

} // namespace strafe::renderer

namespace strafe {

// Loads what the game draws and collects the frame's draws. Usable from
// App::onInit until App::onShutdown returns.
class Renderer {
public:
  Renderer() = delete;

  // Uploads the mesh once. The returned id stays valid until shutdown.
  static std::optional<renderer::MeshId>
  createMesh(std::span<const renderer::Vertex> vertices, std::span<const uint32_t> indices);
  static renderer::MeshId primitive(renderer::Primitive primitive);

  // Uploads the texture once with a full mip chain. The returned id stays
  // valid until shutdown.
  static std::optional<renderer::TextureId>
  createTexture(const renderer::TextureData &data, renderer::ColorSpace color_space);
  // 1x1 white, so an untextured draw keeps its plain color.
  static renderer::TextureId defaultTexture();

  // Compiles shaders/<name>.slang and builds its pipeline.
  static std::optional<gfx::ShaderId> loadShader(std::string_view name);
  static std::optional<renderer::MaterialId> createMaterial(gfx::ShaderId shader);
  // Sets one material field by name.
  static bool setParam(renderer::MaterialId material, std::string_view name, const renderer::MaterialParam &value);

  // The frame being built, for its camera, views and light. Starts each frame
  // default constructed with no draws, before App::onUpdate.
  static renderer::FrameDesc &frame();
  static void draw(const renderer::DrawItem &item);
  // Adds a draw of the OBJ file at path. Caches loaded files and reuses them.
  static bool renderObj(const std::string &path, renderer::MaterialId material, const Transform &transform);

  static void setDebugView(renderer::DebugView view);
  static renderer::DebugView debugView();

private:
  friend class Application;

  static renderer::Renderer &instance();

  // Set by Application while the app runs.
  static renderer::Renderer *renderer_;
};

} // namespace strafe
