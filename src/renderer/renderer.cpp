#include "renderer/renderer.h"

#include "core/log.h"
#include "core/utils.h"
#include "gfx/buffer.h"
#include "gfx/command_list.h"
#include "gfx/device.h"
#include "gfx/framering.h"
#include "gfx/swapchain.h"
#include "gfx/vk_check.h"

#include "vulkan/vulkan_core.h"
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
  glm::mat4 view_projection = glm::mat4(1.0f);
  VkDeviceAddress vertex_buffer = 0;
  glm::vec4 color = glm::vec4(1.0f);
  uint32_t texture_index = 0; // Bindless sampled image slot.
  uint32_t sampler_index = 0; // A gfx::Sampler value.
};

// Must match the push constant block in shader.vert and shader.frag.
static_assert(sizeof(PushConstants) == 160);
static_assert(offsetof(PushConstants, transform) == 0);
static_assert(offsetof(PushConstants, view_projection) == 64);
static_assert(offsetof(PushConstants, vertex_buffer) == 128);
static_assert(offsetof(PushConstants, color) == 136);
static_assert(offsetof(PushConstants, texture_index) == 152);
static_assert(offsetof(PushConstants, sampler_index) == 156);

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

  pipelines_.shutdown();
  lit_shader_ = {};

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

bool Renderer::createPipelines() {
  if (!pipelines_.initialize(*device_, *frame_ring_, bindless_.layout(),
                             sizeof(PushConstants),
                             utils::basePath() + "shaders")) {
    return false;
  }
  pipelines_.registerPass("forward", {.color_formats = {swapchain_->format()},
                                      .depth_format = kDepthFormat});

  const std::optional<gfx::ShaderId> lit = pipelines_.load("lit");
  if (!lit) {
    return false;
  }
  lit_shader_ = *lit;
  return true;
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
    pipelines_.bind(cmd, lit_shader_);
    cmd.bindBindlessSet(bindless_.set());

    for (const DrawItem &draw : desc.draws) {
      assert(draw.mesh.index < meshes_.size() && "DrawItem has no valid mesh");
      if (draw.mesh.index >= meshes_.size()) {
        continue;
      }
      const Mesh &mesh = meshes_[draw.mesh.index];

      // An invalid or stale id falls back to white instead of sampling an
      // empty slot, which would be undefined behaviour on the GPU.
      const TextureId texture_id = draw.texture.index < textures_.size()
                                       ? draw.texture
                                       : default_texture_;
      const Texture &texture = textures_[texture_id.index];

      cmd.bindIndexBuffer(mesh.index_buffer);
      cmd.pushConstants(
          PushConstants{.transform = draw.transform,
                        .view_projection = desc.view_projection,
                        .vertex_buffer = mesh.vertex_buffer.address,
                        .color = draw.color,
                        .texture_index = texture.bindless_slot,
                        .sampler_index = static_cast<uint32_t>(draw.sampler)});
      cmd.drawIndexed(mesh.index_count);
    }
  }
  cmd.endRendering();

  cmd.transition({.image = swapchain_->image(ctx.image_index),
                  .from = gfx::ResourceState::ColorTarget,
                  .to = gfx::ResourceState::Present});
}

} // namespace strafe::renderer
