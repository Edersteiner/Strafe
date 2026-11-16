#pragma once

#include "strafe/gfx/bindless.h"
#include "strafe/gfx/buffer.h"
#include "strafe/gfx/framering.h"
#include "strafe/gfx/image.h"
#include "strafe/gfx/pipeline_manager.h"
#include "strafe/gfx/uploader.h"
#include "strafe/renderer/frame_desc.h"
#include "strafe/renderer/material.h"
#include "strafe/renderer/mesh.h"
#include "strafe/renderer/texture.h"
#include "strafe/renderer/texture_table.h"
#include "strafe/renderer/view_target.h"

#include <volk.h>
#include <vk_mem_alloc.h>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace strafe::gfx {
class Device;
class Swapchain;
} // namespace strafe::gfx

namespace strafe::renderer {

enum class DebugView : std::int8_t { None, Wireframe, Normals, DrawId, Count };

class Renderer {
public:
  Renderer() = default;
  ~Renderer() = default;

  Renderer(const Renderer &) = delete;
  Renderer &operator=(const Renderer &) = delete;

  bool initialize(gfx::Device &device, gfx::Swapchain &swapchain, gfx::FrameRing &frame_ring);

  // Must run after vkDeviceWaitIdle and before FrameRing::shutdown, which
  // flushes the destruction deferred here.
  void shutdown();

  // Resizes the view target to the new swapchain extent.
  bool resize(VkExtent2D extent);

  // Uploads the mesh once. The returned id stays valid until shutdown.
  std::optional<MeshId> createMesh(std::span<const Vertex> vertices, std::span<const uint32_t> indices);

  std::optional<MeshId> uploadMesh(Mesh mesh);

  // Uploads the texture once with a full mip chain. The returned id stays
  // valid until shutdown.
  std::optional<TextureId> createTexture(const TextureData &data, ColorSpace color_space);

  // 1x1 white, so an untextured draw keeps its plain color.
  TextureId defaultTexture() const { return textures_.defaultTexture(); }

  // Compiles shaders/<name>.slang and builds its pipeline.
  std::optional<gfx::ShaderId> loadShader(std::string_view name);

  std::optional<MaterialId> createMaterial(gfx::ShaderId shader);

  // Sets one material field by name.
  bool setParam(MaterialId material, std::string_view name, const MaterialParam &value);

  // Adds a draw of the OBJ file at path. Caches loaded files and reuses them.
  bool renderObj(FrameDesc &frame, const std::string &path, MaterialId material, const Transform &transform);

  void render(const gfx::FrameContext &ctx, const FrameDesc &desc);

  void setDebugView(DebugView view) { debug_view_ = view; }
  DebugView debugView() const { return debug_view_; }

private:
  // Creates the view target or resizes it, then points the composite at it.
  bool updateViewTarget(VkExtent2D extent);
  bool createFrameBuffers();
  bool createPipelines();
  void deferDestroyMesh(const Mesh &mesh);

  gfx::Device *device_ = nullptr;
  gfx::Swapchain *swapchain_ = nullptr;
  gfx::FrameRing *frame_ring_ = nullptr;

  gfx::Uploader uploader_;
  gfx::BindlessSet bindless_;

  TextureTable textures_;
  ViewTarget view_target_;

  // FrameData for each frame in flight, indexed by FrameContext::frame_slot.
  std::array<gfx::Buffer, gfx::FrameRing::kMaxFramesInFlight> frame_buffers_;

  gfx::PipelineManager pipelines_;
  MaterialTable materials_;
  // Indices into FrameDesc::draws in submission order.
  std::vector<uint32_t> draw_order_;

  std::vector<Mesh> meshes_;
  // renderObj cache
  std::unordered_map<std::string, MeshId> obj_meshes_;

  gfx::ShaderId composite_shader_;
  MaterialId composite_material_;

  DebugView debug_view_ = DebugView::None;
  gfx::ShaderId wireframe_shader_;
  gfx::ShaderId normals_shader_;
  gfx::ShaderId drawid_shader_;
};

} // namespace strafe::renderer
