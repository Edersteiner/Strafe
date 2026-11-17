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
  uint32_t draw_id = -1; // For debugging, -1 means not set.
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

  // No swapchain means the window starts minimized. resize() creates the
  // view target once the swapchain exists.
  if (swapchain_->handle() && !updateViewTarget(swapchain_->extent())) {
    log::error("Failed to create the view target");
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

  destroyViewTarget(*device_, *frame_ring_, textures_, view_target_);
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

bool Renderer::resize(VkExtent2D extent) {
  // A minimized window keeps its target, since nothing renders until it's
  // restored.
  if (extent.width == 0 || extent.height == 0) {
    return true;
  }
  return updateViewTarget(extent);
}

bool Renderer::updateViewTarget(VkExtent2D extent) {
  if (textures_.valid(view_target_.color)) {
    if (!resizeViewTarget(*device_, *frame_ring_, textures_, view_target_, extent)) {
      return false;
    }
  } else if (const std::optional<ViewTarget> created = createViewTarget(*device_, textures_, extent)) {
    view_target_ = *created;
  } else {
    return false;
  }
  // The material holds the bindless slot, which changes with every new image.
  return materials_.set(composite_material_, "source", view_target_.color);
}

bool Renderer::createFrameBuffers() {
  // One per frame in flight, so the CPU writes one while
  // the GPU may still be reading the other.
  for (gfx::Buffer &buffer : frame_buffers_) {
    const std::optional<gfx::Buffer> created = gfx::createBuffer(*device_, sizeof(FrameData), 0);
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
  composite_material_ = materials_.create(composite_shader_);
  materials_.set(composite_material_, "sampler", gfx::Sampler::LinearClamp);

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
  assert(textures_.valid(view_target_.color) && "render needs a view target");
  gfx::CommandList cmd(ctx.cmd);
  bindless_bound_ = false;

  // The aspect comes from the target.
  const VkExtent2D extent = view_target_.extent;
  const float aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
  const Mat4 view_projection = projectionMatrix(desc.camera.projection, aspect) * viewMatrix(desc.camera);

  const FrameData frame_data{
      .view_projection = toGlm(view_projection),
      .camera_position = toGlm(desc.camera.transform.position),
      .time = desc.time,
      .light_direction = toGlm(desc.light.direction),
      .light_color = toGlm(desc.light.color),
      .ambient = toGlm(desc.light.ambient)};
  gfx::writeBuffer(*device_, frame_buffers_[ctx.frame_slot], std::as_bytes(std::span(&frame_data, 1)));
  materials_.upload(ctx.frame_slot);

  const VkImage view_color = textures_.image(view_target_.color).handle;
  const VkImage swapchain_image = swapchain_->image(ctx.image_index);

  // The view target is shared by every frame in flight, so clearing it waits
  // for the previous frame's composite reads and depth writes. Between frames
  // the color image is always in ShaderRead.
  const std::array<gfx::ImageTransition, 2> scene_transitions{{
      {.image = view_color,
       .from = gfx::ResourceState::ShaderRead,
       .to = gfx::ResourceState::ColorTarget,
       .discard = true},
      {.image = view_target_.depth.handle,
       .from = gfx::ResourceState::DepthTarget,
       .to = gfx::ResourceState::DepthTarget,
       .discard = true,
       .aspect = view_target_.depth.aspect},
  }};
  cmd.transition(scene_transitions);
  recordScene(cmd, ctx, desc);

  const std::array<gfx::ImageTransition, 2> composite_transitions{{
      {.image = view_color, .from = gfx::ResourceState::ColorTarget, .to = gfx::ResourceState::ShaderRead},
      {.image = swapchain_image,
       .from = gfx::ResourceState::Present,
       .to = gfx::ResourceState::ColorTarget,
       .discard = true},
  }};
  cmd.transition(composite_transitions);
  recordComposite(cmd, ctx);

  cmd.transition(
      {.image = swapchain_image, .from = gfx::ResourceState::ColorTarget, .to = gfx::ResourceState::Present});
}

void Renderer::recordScene(gfx::CommandList &cmd, const gfx::FrameContext &ctx, const FrameDesc &desc) {
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

  const VkExtent2D extent = view_target_.extent;
  const gfx::ColorAttachment color_attachment{
      .view = textures_.image(view_target_.color).view, .clear_color = {0.01f, 0.01f, 0.01f, 1.0f}};
  cmd.beginRendering(
      {.extent = extent,
       .colors = {&color_attachment, 1},
       .depth = gfx::DepthAttachment{.view = view_target_.depth.view}});
  cmd.setViewport(extent);
  cmd.setScissor(extent);

  const gfx::Buffer &frame_buffer = frame_buffers_[ctx.frame_slot];
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
            .frame_data = frame_buffer.address,
            .material = materials_.address(draw.material, ctx.frame_slot),
            .vertex_buffer = mesh.vertex_buffer.address,
            .draw_id = draw_index});
    cmd.drawIndexed(mesh.index_count);
  }
  cmd.endRendering();
}

void Renderer::recordComposite(gfx::CommandList &cmd, const gfx::FrameContext &ctx) {
  const VkExtent2D extent = swapchain_->extent();
  // Black shows wherever no view covers the screen.
  const gfx::ColorAttachment color_attachment{
      .view = swapchain_->imageView(ctx.image_index), .clear_color = {0.0f, 0.0f, 0.0f, 1.0f}};
  cmd.beginRendering({.extent = extent, .colors = {&color_attachment, 1}});
  cmd.setViewport(extent);
  cmd.setScissor(extent);

  // A fullscreen triangle built from the vertex id, so no buffers.
  bindShader(cmd, composite_shader_);
  cmd.pushConstants(PushConstants{.material = materials_.address(composite_material_, ctx.frame_slot)});
  cmd.draw(3);
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
