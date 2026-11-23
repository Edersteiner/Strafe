#include "strafe/renderer/renderer.h"

#include "strafe/core/log.h"
#include "strafe/core/utils.h"
#include "strafe/gfx/bindless.h"
#include "strafe/gfx/buffer.h"
#include "strafe/gfx/command_list.h"
#include "strafe/gfx/device.h"
#include "strafe/gfx/framering.h"
#include "strafe/gfx/pipeline_manager.h"
#include "strafe/gfx/swapchain.h"
#include "strafe/renderer/obj_loader.h"
#include "strafe/renderer/view_target.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "core/glm_interop.h"

#include <glm/glm.hpp>

namespace strafe::renderer {

namespace {
struct PushConstants {
  glm::mat4 transform = glm::mat4(1.0f);
  VkDeviceAddress frame_data = 0; // This frame's FrameData buffer.
  VkDeviceAddress material = 0;   // The draw's material block, or 0.
  VkDeviceAddress vertex_buffer = 0;
  uint32_t draw_id = UINT32_MAX; // For debugging, UINT32_MAX means not set.
  uint32_t padding = 0;
};

static_assert(sizeof(PushConstants) == 96);
static_assert(offsetof(PushConstants, transform) == 0);
static_assert(offsetof(PushConstants, frame_data) == 64);
static_assert(offsetof(PushConstants, material) == 72);
static_assert(offsetof(PushConstants, vertex_buffer) == 80);
static_assert(offsetof(PushConstants, draw_id) == 88);

struct FrameData {
  glm::mat4 view_projection = glm::mat4(1.0f);
  glm::vec3 camera_position{0.0f};
  float time = 0.0f;
  glm::vec3 light_direction{0.0f, -1.0f, 0.0f};
  glm::vec3 light_color{1.0f};
  glm::vec3 ambient{0.0f};
};

static_assert(sizeof(FrameData) == 116);
static_assert(offsetof(FrameData, camera_position) == 64);
static_assert(offsetof(FrameData, time) == 76);
static_assert(offsetof(FrameData, light_direction) == 80);
static_assert(offsetof(FrameData, light_color) == 92);
static_assert(offsetof(FrameData, ambient) == 104);

// Each view's FrameData block starts 16 byte aligned.
constexpr VkDeviceSize kFrameDataStride = (sizeof(FrameData) + 15) / 16 * 16;

VkRect2D pixelRect(const ScreenRegion &region, VkExtent2D extent) {
  const auto edge = [](float t, uint32_t size) {
    return static_cast<int32_t>(std::lround(std::clamp(t, 0.0f, 1.0f) * static_cast<float>(size)));
  };
  const int32_t x0 = edge(region.x, extent.width);
  const int32_t y0 = edge(region.y, extent.height);
  const int32_t x1 = edge(region.x + region.width, extent.width);
  const int32_t y1 = edge(region.y + region.height, extent.height);
  return {
      .offset = {.x = x0, .y = y0},
      .extent = {
          .width = static_cast<uint32_t>(std::max(x1 - x0, 0)), .height = static_cast<uint32_t>(std::max(y1 - y0, 0))}};
}

VkExtent2D scaledExtent(VkExtent2D extent, float scale) {
  const auto scaled = [scale](uint32_t size) {
    return static_cast<uint32_t>(std::max(std::lround(static_cast<float>(size) * scale), 1L));
  };
  return {.width = scaled(extent.width), .height = scaled(extent.height)};
}

} // namespace

bool Renderer::initialize(gfx::Device &device, gfx::Swapchain &swapchain, gfx::FrameRing &frame_ring) {
  device_ = &device;
  swapchain_ = &swapchain;
  frame_ring_ = &frame_ring;

  if (!uploader_.initialize(device)) {
    log::error("Failed to create the uploader");
    return false;
  }
  if (!bindless_.initialize(device)) {
    log::error("Failed to create the bindless descriptor set");
    return false;
  }
  if (!createFrameBuffers()) {
    log::error("Failed to create the per-frame buffers");
    return false;
  }

  if (!textures_.initialize(device, frame_ring, uploader_, bindless_)) {
    return false;
  }
  if (!createPipelines()) {
    log::error("Failed to create the pipelines");
    return false;
  }
  return true;
}

void Renderer::shutdown() {
  if (!device_) {
    return;
  }

  materials_.shutdown();
  pipelines_.shutdown();

  for (const Mesh &mesh : meshes_) {
    deferDestroyMesh(mesh);
  }
  meshes_.clear();
  obj_meshes_.clear();
  primitive_meshes_ = {};

  for (View &view : views_) {
    destroyView(view);
  }
  views_.clear();
  free_composite_materials_.clear();
  textures_.shutdown();

  for (gfx::Buffer &buffer : frame_buffers_) {
    if (buffer.handle) {
      frame_ring_->deferDestroy([gfx_device = device_, buffer] { gfx::destroyBuffer(*gfx_device, buffer); });
    }
    buffer = {};
  }

  // Both safe to destroy now, because the GPU is idle.
  bindless_.shutdown();
  uploader_.shutdown();
}

std::optional<uint32_t> Renderer::prepareView(ViewId id, VkExtent2D target_extent) {
  const auto it = std::ranges::find(views_, id, &View::id);
  uint32_t index = 0;
  if (it == views_.end()) {
    const std::optional<ViewTarget> target = createViewTarget(*device_, textures_, target_extent);
    if (!target) {
      return std::nullopt;
    }
    MaterialId material;
    if (!free_composite_materials_.empty()) {
      material = free_composite_materials_.back();
      free_composite_materials_.pop_back();
    } else {
      material = materials_.create(composite_shader_);
      materials_.set(material, "sampler", gfx::Sampler::LinearClamp);
    }
    views_.push_back({.id = id, .target = *target, .composite_material = material});
    index = static_cast<uint32_t>(views_.size() - 1);
  } else {
    index = static_cast<uint32_t>(it - views_.begin());
    View &view = views_[index];
    const bool same_size =
        view.target.extent.width == target_extent.width && view.target.extent.height == target_extent.height;
    if (same_size) {
      view.last_used_frame = frame_count_;
      return index;
    }
    if (!resizeViewTarget(*device_, *frame_ring_, textures_, view.target, target_extent)) {
      return std::nullopt;
    }
  }

  View &view = views_[index];
  view.last_used_frame = frame_count_;
  // The material holds the bindless slot, which changes with every new image.
  materials_.set(view.composite_material, "source", view.target.color);
  return index;
}

void Renderer::evictUnusedViews() {
  for (View &view : views_) {
    if (frame_count_ - view.last_used_frame > kViewKeepFrames) {
      destroyView(view);
    }
  }
  std::erase_if(views_, [](const View &view) { return !view.composite_material.valid(); });
}

void Renderer::destroyView(View &view) {
  destroyViewTarget(*device_, *frame_ring_, textures_, view.target);
  if (view.composite_material.valid()) {
    free_composite_materials_.push_back(view.composite_material);
  }
  view.composite_material = {};
}

bool Renderer::createFrameBuffers() {
  // One per frame in flight, so the CPU writes one while
  // the GPU may still be reading the other.
  for (gfx::Buffer &buffer : frame_buffers_) {
    const std::optional<gfx::Buffer> created = gfx::createBuffer(*device_, kMaxViews * kFrameDataStride, 0);
    if (!created) {
      return false;
    }
    buffer = *created;
  }
  return true;
}

bool Renderer::createPipelines() {
  // The app's shaders come first, so it can add its own and replace engine
  // ones by name. The strafe.app rule stages both next to the exe.
  std::vector<std::string> shader_directories{utils::basePath() + "shaders", utils::basePath() + "engine/shaders"};
  if (!pipelines_.initialize(
          *device_, *frame_ring_, bindless_.layout(),
          {.push_constants = sizeof(PushConstants), .frame_data = sizeof(FrameData)}, std::move(shader_directories))) {
    return false;
  }

  pipelines_.registerPass("forward", {.color_formats = {kViewColorFormat}, .depth_format = kViewDepthFormat});
  pipelines_.registerPass("composite", {.color_formats = {swapchain_->format()}});
  materials_.initialize(*device_, *frame_ring_, pipelines_, [this](TextureId id) { return textures_.slot(id); });

// Load debug view shaders in debug mode
#ifdef STRAFE_DEBUG
  wireframe_shader_ = *pipelines_.load("debug/wireframe");
  normals_shader_ = *pipelines_.load("debug/normals");
  drawid_shader_ = *pipelines_.load("debug/drawid");
#endif

  const std::optional<gfx::ShaderId> composite = pipelines_.load("composite");
  if (!composite) {
    log::error("Failed to load the composite shader");
    return false;
  }

  composite_shader_ = *composite;

  return true;
}

std::optional<gfx::ShaderId> Renderer::loadShader(std::string_view name) { return pipelines_.load(name); }

std::optional<MaterialId> Renderer::createMaterial(gfx::ShaderId shader) {
  if (!shader.valid()) {
    log::error("createMaterial needs a shader from loadShader");
    return std::nullopt;
  }
  return materials_.create(shader);
}

bool Renderer::setParam(MaterialId material, std::string_view name, const MaterialParam &value) {
  return materials_.set(material, name, value);
}

std::optional<MeshId> Renderer::createMesh(std::span<const Vertex> vertices, std::span<const uint32_t> indices) {
  if (vertices.empty() || indices.empty()) {
    log::error("A mesh needs at least one vertex and one index");
    return std::nullopt;
  }
  // Vertex pulling reads through a raw address without bounds checking, so an
  // out of range index would read past the end of the vertex buffer.
  for (const uint32_t index : indices) {
    if (index >= vertices.size()) {
      log::error("Mesh index {} is out of range for {} vertices", index, vertices.size());
      return std::nullopt;
    }
  }

  const std::span<const std::byte> vertex_bytes = std::as_bytes(vertices);
  const std::span<const std::byte> index_bytes = std::as_bytes(indices);

  // Both live in device local memory and are filled once through the
  // uploader. The vertex buffer is only read through its device address, which
  // createBuffer always adds.
  const std::optional<gfx::Buffer> vertex_buffer = gfx::createBuffer(
      *device_, vertex_bytes.size(), VK_BUFFER_USAGE_TRANSFER_DST_BIT, gfx::BufferMemory::DeviceLocal);
  const std::optional<gfx::Buffer> index_buffer = gfx::createBuffer(
      *device_, index_bytes.size(), VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
      gfx::BufferMemory::DeviceLocal);

  Mesh mesh{.index_count = static_cast<uint32_t>(indices.size())};
  if (vertex_buffer) {
    mesh.vertex_buffer = *vertex_buffer;
  }
  if (index_buffer) {
    mesh.index_buffer = *index_buffer;
  }

  if (!vertex_buffer || !index_buffer || !uploader_.uploadBuffer(mesh.vertex_buffer, vertex_bytes) ||
      !uploader_.uploadBuffer(mesh.index_buffer, index_bytes)) {
    log::error("Failed to create mesh buffers");
    deferDestroyMesh(mesh);
    return std::nullopt;
  }

  meshes_.push_back(mesh);
  return MeshId{.index = static_cast<uint32_t>(meshes_.size() - 1)};
}

MeshId Renderer::primitive(Primitive primitive) {
  std::optional<MeshId> &mesh = primitive_meshes_.at(static_cast<size_t>(primitive));
  if (!mesh) {
    const MeshData data = primitiveMeshData(primitive);
    mesh = createMesh(data.vertices, data.indices).value_or(MeshId{});
  }
  return *mesh;
}

bool Renderer::renderObj(FrameDesc &frame, const std::string &path, MaterialId material, const Transform &transform) {
  auto it = obj_meshes_.find(path);
  if (it == obj_meshes_.end()) {
    MeshId mesh{};
    if (const std::optional<MeshData> data = loadFromObj(path.c_str())) {
      mesh = createMesh(data->vertices, data->indices).value_or(MeshId{});
    }
    it = obj_meshes_.emplace(path, mesh).first;
  }

  if (!it->second.valid()) {
    return false;
  }
  frame.draws.push_back({.mesh = it->second, .material = material, .transform = transform});
  return true;
}

std::optional<TextureId> Renderer::createTexture(const TextureData &data, ColorSpace color_space) {
  return textures_.create(data, color_space);
}

void Renderer::deferDestroyMesh(const Mesh &mesh) {
  frame_ring_->deferDestroy([gfx_device = device_, mesh] {
    gfx::destroyBuffer(*gfx_device, mesh.vertex_buffer);
    gfx::destroyBuffer(*gfx_device, mesh.index_buffer);
  });
}

void Renderer::render(const gfx::FrameContext &ctx, const FrameDesc &desc) {
  gfx::CommandList cmd(ctx.cmd);
  bindless_bound_ = false;
  ++frame_count_;
  // First, so indices into views_ stay valid for the rest of the frame.
  evictUnusedViews();

  // A game that pushes no views gets one view of desc.camera over the window.
  const ViewDesc default_view{.id = kDefaultViewId, .camera = desc.camera};
  const std::span<const ViewDesc> view_descs =
      desc.views.empty() ? std::span<const ViewDesc>(&default_view, 1) : std::span<const ViewDesc>(desc.views);
  assert(view_descs.size() <= kMaxViews && "Too many views in FrameDesc");
  const gfx::Buffer &frame_buffer = frame_buffers_[ctx.frame_slot];
  const VkExtent2D screen = swapchain_->extent();
  std::array<FrameView, kMaxViews> frame_views{};
  uint32_t view_count = 0;
  for (const ViewDesc &view_desc : view_descs) {
    if (view_count == kMaxViews) {
      break;
    }
    const std::span<const FrameView> earlier(frame_views.data(), view_count);
    const bool duplicate =
        std::ranges::any_of(earlier, [&](const FrameView &other) { return other.desc->id == view_desc.id; });
    assert(!duplicate && "Two views in one frame share an id");
    const VkRect2D rect = pixelRect(view_desc.region, screen);
    if (duplicate || rect.extent.width == 0 || rect.extent.height == 0) {
      continue;
    }
    const std::optional<uint32_t> index =
        prepareView(view_desc.id, scaledExtent(rect.extent, view_desc.resolution_scale));
    if (!index) {
      continue;
    }

    // The aspect comes from the target, so a half screen view gets its own.
    const VkExtent2D extent = views_[*index].target.extent;
    const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
    const Mat4 view_projection = projectionMatrix(view_desc.camera.projection, aspect) * viewMatrix(view_desc.camera);
    const FrameData frame_data{
        .view_projection = toGlm(view_projection),
        .camera_position = toGlm(view_desc.camera.transform.position),
        .time = desc.time,
        .light_direction = toGlm(desc.light.direction),
        .light_color = toGlm(desc.light.color),
        .ambient = toGlm(desc.light.ambient)};
    const VkDeviceSize offset = view_count * kFrameDataStride;
    gfx::writeBuffer(*device_, frame_buffer, std::as_bytes(std::span(&frame_data, 1)), offset);

    frame_views[view_count++] = {
        .desc = &view_desc, .view_index = *index, .rect = rect, .frame_data = frame_buffer.address + offset};
  }
  const std::span<const FrameView> views(frame_views.data(), view_count);

  materials_.upload(ctx.frame_slot);
  sortDraws(desc);

  static_assert(2 * kMaxViews <= gfx::CommandList::kMaxTransitions);
  std::array<gfx::ImageTransition, 2 * kMaxViews> scene_transitions{};
  for (uint32_t i = 0; i < view_count; ++i) {
    const ViewTarget &target = views_[views[i].view_index].target;
    scene_transitions[2 * i] = {
        .image = textures_.image(target.color).handle,
        .from = gfx::ResourceState::ShaderRead,
        .to = gfx::ResourceState::ColorTarget,
        .discard = true};
    scene_transitions[2 * i + 1] = {
        .image = target.depth.handle,
        .from = gfx::ResourceState::DepthTarget,
        .to = gfx::ResourceState::DepthTarget,
        .discard = true,
        .aspect = target.depth.aspect};
  }
  cmd.transition(std::span(scene_transitions.data(), 2 * view_count));
  for (const FrameView &view : views) {
    recordScene(cmd, ctx, desc, view);
  }

  const VkImage swapchain_image = swapchain_->image(ctx.image_index);
  static_assert(kMaxViews + 1 <= gfx::CommandList::kMaxTransitions);
  std::array<gfx::ImageTransition, kMaxViews + 1> composite_transitions{};
  for (uint32_t i = 0; i < view_count; ++i) {
    composite_transitions[i] = {
        .image = textures_.image(views_[views[i].view_index].target.color).handle,
        .from = gfx::ResourceState::ColorTarget,
        .to = gfx::ResourceState::ShaderRead};
  }
  composite_transitions[view_count] = {
      .image = swapchain_image,
      .from = gfx::ResourceState::Present,
      .to = gfx::ResourceState::ColorTarget,
      .discard = true};
  cmd.transition(std::span(composite_transitions.data(), view_count + 1));
  recordComposite(cmd, ctx, views);

  cmd.transition(
      {.image = swapchain_image, .from = gfx::ResourceState::ColorTarget, .to = gfx::ResourceState::Present});
}

void Renderer::sortDraws(const FrameDesc &desc) {
  // Opaque draws first, then blended ones over them.
  draw_order_.clear();
  for (uint32_t i = 0; i < desc.draws.size(); ++i) {
    const DrawItem &draw = desc.draws[i];
    const bool valid = draw.mesh.index < meshes_.size() && materials_.valid(draw.material);
    assert(valid && "DrawItem needs a valid mesh and material");
    if (valid) {
      draw_order_.push_back(i);
    }
  }
  const auto sort_key = [&](uint32_t draw_index) {
    const gfx::ShaderId shader = materials_.shader(desc.draws[draw_index].material);
    const bool blended = pipelines_.reflection(shader).blend != gfx::BlendMode::Opaque;
    return std::pair(blended, shader.index);
  };
  std::ranges::stable_sort(draw_order_, [&](uint32_t a, uint32_t b) { return sort_key(a) < sort_key(b); });
}

void Renderer::recordScene(
    gfx::CommandList &cmd, const gfx::FrameContext &ctx, const FrameDesc &desc, const FrameView &view) {
  const ViewTarget &target = views_[view.view_index].target;
  const gfx::ColorAttachment color_attachment{
      .view = textures_.image(target.color).view, .clear_color = view.desc->clear_color};
  cmd.beginRendering(
      {.extent = target.extent,
       .colors = {&color_attachment, 1},
       .depth = gfx::DepthAttachment{.view = target.depth.view}});
  cmd.setViewport(target.extent);
  cmd.setScissor(target.extent);

  gfx::ShaderId bound_shader;
  for (const uint32_t draw_index : draw_order_) {
    const DrawItem &draw = desc.draws[draw_index];
    const gfx::ShaderId shader = materials_.shader(draw.material);
    if (shader.index != bound_shader.index) {
#ifdef STRAFE_DEBUG
      if (debug_view_ == DebugView::Wireframe) {
        bindShader(cmd, wireframe_shader_);
      } else if (debug_view_ == DebugView::Normals) {
        bindShader(cmd, normals_shader_);
      } else if (debug_view_ == DebugView::DrawId) {
        bindShader(cmd, drawid_shader_);
      } else {
        bindShader(cmd, shader);
      }
#else
      bindShader(cmd, shader);
#endif
      bound_shader = shader;
    }

    const Mesh &mesh = meshes_[draw.mesh.index];
    cmd.bindIndexBuffer(mesh.index_buffer);
    cmd.pushConstants(
        PushConstants{
            .transform = toGlm(draw.transform.toMatrix()),
            .frame_data = view.frame_data,
            .material = materials_.address(draw.material, ctx.frame_slot),
            .vertex_buffer = mesh.vertex_buffer.address,
            .draw_id = draw_index});
    cmd.drawIndexed(mesh.index_count);
  }
  cmd.endRendering();
}

void Renderer::recordComposite(gfx::CommandList &cmd, const gfx::FrameContext &ctx, std::span<const FrameView> views) {
  // Black shows wherever no view covers the screen.
  const gfx::ColorAttachment color_attachment{
      .view = swapchain_->imageView(ctx.image_index), .clear_color = {0.0f, 0.0f, 0.0f, 1.0f}};
  cmd.beginRendering({.extent = swapchain_->extent(), .colors = {&color_attachment, 1}});
  if (!views.empty()) {
    bindShader(cmd, composite_shader_);
  }

  for (const FrameView &view : views) {
    cmd.setViewport(view.rect);
    cmd.setScissor(view.rect);
    cmd.pushConstants(
        PushConstants{.material = materials_.address(views_[view.view_index].composite_material, ctx.frame_slot)});
    cmd.draw(3);
  }

  if (overlay_) {
    overlay_(cmd);
    // The overlay should bind its own pipeline layout and sets
    bindless_bound_ = false;
  }

  cmd.endRendering();
}

void Renderer::bindShader(gfx::CommandList &cmd, gfx::ShaderId shader) {
  pipelines_.bind(cmd, shader);
  // Every pipeline shares the layout, so the set stays bound across later
  // pipeline changes and passes.
  if (!bindless_bound_) {
    cmd.bindBindlessSet(bindless_.set());
    bindless_bound_ = true;
  }
}

} // namespace strafe::renderer
