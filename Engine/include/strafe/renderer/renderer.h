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
class CommandList;
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

  // Renders every view in desc into its own target, then composites them
  // into their regions of the swapchain image.
  void render(const gfx::FrameContext &ctx, const FrameDesc &desc);

  void setDebugView(DebugView view) { debug_view_ = view; }
  DebugView debugView() const { return debug_view_; }

private:
  static constexpr uint32_t kMaxViews = 4;
  // A view missing from FrameDesc keeps its target this many frames, so
  // toggling splitscreen doesn't recreate images.
  static constexpr uint64_t kViewKeepFrames = 120;

  struct View {
    ViewId id;
    ViewTarget target;
    // Each view needs its own, because the source texture is a parameter.
    MaterialId composite_material;
    uint64_t last_used_frame = 0;
  };

  // A view of this frame, matched to its state.
  struct FrameView {
    const ViewDesc *desc = nullptr;
    // Into views_, stable for the frame because eviction runs first.
    uint32_t view_index = 0;
    // The region in swapchain pixels.
    VkRect2D rect{};
    VkDeviceAddress frame_data = 0;
  };

  // Finds or creates the view and sizes its target, returning its index in
  // views_. Must run before the material upload, since a new image sets the
  // composite source again.
  std::optional<uint32_t> prepareView(ViewId id, VkExtent2D target_extent);
  void evictUnusedViews();
  void destroyView(View &view);
  void sortDraws(const FrameDesc &desc);
  bool createFrameBuffers();
  bool createPipelines();
  void deferDestroyMesh(const Mesh &mesh);

  // render() records every barrier, the passes only record their draws.
  void recordScene(gfx::CommandList &cmd, const gfx::FrameContext &ctx, const FrameDesc &desc, const FrameView &view);
  void recordComposite(gfx::CommandList &cmd, const gfx::FrameContext &ctx, std::span<const FrameView> views);
  // Binds the bindless set with the frame's first pipeline, whichever pass
  // that is in.
  void bindShader(gfx::CommandList &cmd, gfx::ShaderId shader);

  gfx::Device *device_ = nullptr;
  gfx::Swapchain *swapchain_ = nullptr;
  gfx::FrameRing *frame_ring_ = nullptr;

  gfx::Uploader uploader_;
  gfx::BindlessSet bindless_;

  TextureTable textures_;
  std::vector<View> views_;
  // Composite materials of evicted views, reused by new ones.
  std::vector<MaterialId> free_composite_materials_;
  // Counts render() calls, for evicting unused views.
  uint64_t frame_count_ = 0;

  // kMaxViews FrameData blocks for each frame in flight, indexed by
  // FrameContext::frame_slot.
  std::array<gfx::Buffer, gfx::FrameRing::kMaxFramesInFlight> frame_buffers_;

  gfx::PipelineManager pipelines_;
  MaterialTable materials_;
  // Indices into FrameDesc::draws in submission order.
  std::vector<uint32_t> draw_order_;

  std::vector<Mesh> meshes_;
  // renderObj cache
  std::unordered_map<std::string, MeshId> obj_meshes_;

  gfx::ShaderId composite_shader_;
  // Reset by render(), set by bindShader.
  bool bindless_bound_ = false;

  DebugView debug_view_ = DebugView::None;
  gfx::ShaderId wireframe_shader_;
  gfx::ShaderId normals_shader_;
  gfx::ShaderId drawid_shader_;
};

} // namespace strafe::renderer
