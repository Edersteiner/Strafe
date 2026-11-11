#include "renderer/renderer.h"

#include "core/log.h"
#include "core/utils.h"
#include "gfx/buffer.h"
#include "gfx/command_list.h"
#include "gfx/device.h"
#include "gfx/framering.h"
#include "gfx/swapchain.h"

#include "vulkan/vulkan_core.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace strafe::renderer {

namespace {
struct PushConstants {
  glm::mat4 transform = glm::mat4(1.0f);
  VkDeviceAddress frame_data = 0; // This frame's FrameData buffer.
  VkDeviceAddress material = 0;   // The draw's material block, or 0.
  VkDeviceAddress vertex_buffer = 0;
};

static_assert(sizeof(PushConstants) == 88);
static_assert(offsetof(PushConstants, transform) == 0);
static_assert(offsetof(PushConstants, frame_data) == 64);
static_assert(offsetof(PushConstants, material) == 72);
static_assert(offsetof(PushConstants, vertex_buffer) == 80);

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

bool Renderer::initialize(gfx::Device &device, gfx::Swapchain &swapchain,
                          gfx::FrameRing &frame_ring) {
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

  const TextureData white{.width = 1,
                          .height = 1,
                          .rgba = {std::byte{0xff}, std::byte{0xff},
                                   std::byte{0xff}, std::byte{0xff}}};
  if (const std::optional<TextureId> id =
          createTexture(white, ColorSpace::Srgb)) {
    default_texture_ = *id;
  } else {
    log::error("Failed to create the default texture");
    return false;
  }

  // No swapchain means the window starts minimized. resize() creates the
  // depth buffer once the swapchain exists.
  if (swapchain_->handle() && !createDepthBuffer(swapchain_->extent())) {
    log::error("Failed to create depth buffer");
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

  // The whole bindless set goes away below, so the slots aren't freed one by
  // one.
  for (const Texture &texture : textures_) {
    frame_ring_->deferDestroy([gfx_device = device_, image = texture.image] {
      gfx::destroyImage(*gfx_device, image);
    });
  }
  textures_.clear();
  default_texture_ = {};

  for (gfx::Buffer &buffer : frame_buffers_) {
    if (buffer.handle) {
      frame_ring_->deferDestroy([gfx_device = device_, buffer] {
        gfx::destroyBuffer(*gfx_device, buffer);
      });
    }
    buffer = {};
  }

  destroyDepthBuffer();
  // Both safe to destroy now, because the GPU is idle.
  bindless_.shutdown();
  uploader_.shutdown();
}

bool Renderer::resize(VkExtent2D extent) {
  destroyDepthBuffer();
  if (extent.width == 0 || extent.height == 0) {
    return true;
  }
  return createDepthBuffer(extent);
}

bool Renderer::createDepthBuffer(VkExtent2D extent) {
  const std::optional<gfx::Image> depth = gfx::createImage(
      *device_, {.extent = extent,
                 .format = kDepthFormat,
                 .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                 .dedicated = true});
  if (!depth) {
    return false;
  }
  depth_image_ = *depth;
  return true;
}

void Renderer::destroyDepthBuffer() {
  gfx::destroyImage(*device_, depth_image_);
  depth_image_ = {};
}

bool Renderer::createFrameBuffers() {
  // One per frame in flight, so the CPU writes one while
  // the GPU may still be reading the other.
  for (gfx::Buffer &buffer : frame_buffers_) {
    const std::optional<gfx::Buffer> created =
        gfx::createBuffer(*device_, sizeof(FrameData), 0);
    if (!created) {
      return false;
    }
    buffer = *created;
  }
  return true;
}

bool Renderer::createPipelines() {
  if (!pipelines_.initialize(*device_, *frame_ring_, bindless_.layout(),
                             {.push_constants = sizeof(PushConstants),
                              .frame_data = sizeof(FrameData)},
                             utils::basePath() + "shaders")) {
    return false;
  }
  pipelines_.registerPass("forward", {.color_formats = {swapchain_->format()},
                                      .depth_format = kDepthFormat});
  materials_.initialize(*device_, *frame_ring_, pipelines_,
                        [this](TextureId id) { return textureSlot(id); });
  return true;
}

std::optional<gfx::ShaderId> Renderer::loadShader(std::string_view name) {
  return pipelines_.load(name);
}

std::optional<MaterialId> Renderer::createMaterial(gfx::ShaderId shader) {
  if (!shader.valid()) {
    log::error("createMaterial needs a shader from loadShader");
    return std::nullopt;
  }
  return materials_.create(shader);
}

bool Renderer::setParam(MaterialId material, std::string_view name,
                        const MaterialParam &value) {
  return materials_.set(material, name, value);
}

uint32_t Renderer::textureSlot(TextureId id) const {
  // An invalid or stale id falls back to white instead of sampling an empty
  // slot, which would be undefined behaviour on the GPU.
  const TextureId resolved =
      id.index < textures_.size() ? id : default_texture_;
  return textures_[resolved.index].bindless_slot;
}

std::optional<MeshId> Renderer::createMesh(std::span<const Vertex> vertices,
                                           std::span<const uint32_t> indices) {
  if (vertices.empty() || indices.empty()) {
    log::error("A mesh needs at least one vertex and one index");
    return std::nullopt;
  }
  // Vertex pulling reads through a raw address without bounds checking, so an
  // out of range index would read past the end of the vertex buffer.
  for (const uint32_t index : indices) {
    if (index >= vertices.size()) {
      log::error("Mesh index {} is out of range for {} vertices", index,
                 vertices.size());
      return std::nullopt;
    }
  }

  const std::span<const std::byte> vertex_bytes = std::as_bytes(vertices);
  const std::span<const std::byte> index_bytes = std::as_bytes(indices);

  // Both live in device local memory and are filled once through the
  // uploader. The vertex buffer is only read through its device address, which
  // createBuffer always adds.
  const std::optional<gfx::Buffer> vertex_buffer = gfx::createBuffer(
      *device_, vertex_bytes.size(), VK_BUFFER_USAGE_TRANSFER_DST_BIT,
      gfx::BufferMemory::DeviceLocal);
  const std::optional<gfx::Buffer> index_buffer = gfx::createBuffer(
      *device_, index_bytes.size(),
      VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
      gfx::BufferMemory::DeviceLocal);

  Mesh mesh{.index_count = static_cast<uint32_t>(indices.size())};
  if (vertex_buffer) {
    mesh.vertex_buffer = *vertex_buffer;
  }
  if (index_buffer) {
    mesh.index_buffer = *index_buffer;
  }

  if (!vertex_buffer || !index_buffer ||
      !uploader_.uploadBuffer(mesh.vertex_buffer, vertex_bytes) ||
      !uploader_.uploadBuffer(mesh.index_buffer, index_bytes)) {
    log::error("Failed to create mesh buffers");
    deferDestroyMesh(mesh);
    return std::nullopt;
  }

  meshes_.push_back(mesh);
  return MeshId{.index = static_cast<uint32_t>(meshes_.size() - 1)};
}

std::optional<TextureId> Renderer::createTexture(const TextureData &data,
                                                 ColorSpace color_space) {
  const std::optional<gfx::Image> image = gfx::createImage(
      *device_,
      {.extent = {.width = data.width, .height = data.height},
       .format = color_space == ColorSpace::Srgb ? VK_FORMAT_R8G8B8A8_SRGB
                                                 : VK_FORMAT_R8G8B8A8_UNORM,
       .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT});
  if (!image) {
    log::error("Failed to create a {}x{} texture", data.width, data.height);
    return std::nullopt;
  }

  // Validates the byte count against the extent and format.
  if (!uploader_.uploadImage(*image, data.rgba)) {
    // Nothing on the GPU uses it, and the uploader has finished with it.
    gfx::destroyImage(*device_, *image);
    return std::nullopt;
  }

  const std::optional<uint32_t> slot = bindless_.addSampledImage(image->view);
  if (!slot) {
    gfx::destroyImage(*device_, *image);
    return std::nullopt;
  }

  textures_.push_back({.image = *image, .bindless_slot = *slot});
  return TextureId{.index = static_cast<uint32_t>(textures_.size() - 1)};
}

void Renderer::deferDestroyMesh(const Mesh &mesh) {
  frame_ring_->deferDestroy([gfx_device = device_, mesh] {
    gfx::destroyBuffer(*gfx_device, mesh.vertex_buffer);
    gfx::destroyBuffer(*gfx_device, mesh.index_buffer);
  });
}

void Renderer::render(const gfx::FrameContext &ctx, const FrameDesc &desc) {
  const VkExtent2D extent = swapchain_->extent();
  gfx::CommandList cmd(ctx.cmd);

  const gfx::Buffer &frame_buffer = frame_buffers_[ctx.frame_slot];
  const FrameData frame_data{.view_projection = desc.view_projection,
                             .camera_position = desc.camera_position,
                             .time = desc.time,
                             .light_direction = desc.light.direction,
                             .light_color = desc.light.color,
                             .ambient = desc.light.ambient};
  gfx::writeBuffer(*device_, frame_buffer,
                   std::as_bytes(std::span(&frame_data, 1)));
  materials_.upload(ctx.frame_slot);

  // Opaque draws first, then blended ones over them.
  draw_order_.clear();
  for (uint32_t i = 0; i < desc.draws.size(); ++i) {
    const DrawItem &draw = desc.draws[i];
    const bool valid =
        draw.mesh.index < meshes_.size() && materials_.valid(draw.material);
    assert(valid && "DrawItem needs a valid mesh and material");
    if (valid) {
      draw_order_.push_back(i);
    }
  }
  const auto sort_key = [&](uint32_t draw_index) {
    const gfx::ShaderId shader =
        materials_.shader(desc.draws[draw_index].material);
    const bool blended =
        pipelines_.reflection(shader).blend != gfx::BlendMode::Opaque;
    return std::pair(blended, shader.index);
  };
  std::stable_sort(
      draw_order_.begin(), draw_order_.end(),
      [&](uint32_t a, uint32_t b) { return sort_key(a) < sort_key(b); });

  // Both targets are cleared, so their old contents are discarded. The depth
  // image is shared by every frame in flight, so it still waits for the
  // previous frame's depth writes.
  const std::array<gfx::ImageTransition, 2> begin_transitions{{
      {.image = swapchain_->image(ctx.image_index),
       .from = gfx::ResourceState::Present,
       .to = gfx::ResourceState::ColorTarget,
       .discard = true},
      {.image = depth_image_.handle,
       .from = gfx::ResourceState::DepthTarget,
       .to = gfx::ResourceState::DepthTarget,
       .discard = true,
       .aspect = depth_image_.aspect},
  }};
  cmd.transition(begin_transitions);

  const gfx::ColorAttachment color_attachment{
      .view = swapchain_->imageView(ctx.image_index),
      .clear_color = {0.01f, 0.01f, 0.01f, 1.0f}};

  cmd.beginRendering(
      {.extent = extent,
       .colors = {&color_attachment, 1},
       .depth = gfx::DepthAttachment{.view = depth_image_.view}});
  {
    cmd.setViewport(extent);
    cmd.setScissor(extent);

    gfx::ShaderId bound_shader;
    for (const uint32_t draw_index : draw_order_) {
      const DrawItem &draw = desc.draws[draw_index];
      const gfx::ShaderId shader = materials_.shader(draw.material);
      if (shader.index != bound_shader.index) {
        pipelines_.bind(cmd, shader);
        // Every pipeline shares the layout, so the set stays bound across
        // later pipeline changes.
        if (!bound_shader.valid()) {
          cmd.bindBindlessSet(bindless_.set());
        }
        bound_shader = shader;
      }

      const Mesh &mesh = meshes_[draw.mesh.index];
      cmd.bindIndexBuffer(mesh.index_buffer);
      cmd.pushConstants(PushConstants{
          .transform = draw.transform,
          .frame_data = frame_buffer.address,
          .material = materials_.address(draw.material, ctx.frame_slot),
          .vertex_buffer = mesh.vertex_buffer.address});
      cmd.drawIndexed(mesh.index_count);
    }
  }
  cmd.endRendering();

  cmd.transition({.image = swapchain_->image(ctx.image_index),
                  .from = gfx::ResourceState::ColorTarget,
                  .to = gfx::ResourceState::Present});
}

} // namespace strafe::renderer
