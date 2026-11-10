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

// Todo: replace shaderc and glsl with Slang.
#include <shaderc/shaderc.hpp>

#include <glm/glm.hpp>

// This currently hardcodes pipelines and drawing
// but this will eventually serve as the orchestrator
// for other general purpose rendering systems.
namespace strafe::renderer {

namespace {
VkShaderModule createShaderModule(VkDevice device, const std::string &file_name,
                                  shaderc_shader_kind kind) {
  // Read shader file from disk
  const std::string shader_path = utils::basePath() + "shaders/" + file_name;
  const std::string src = utils::readTextFile(shader_path);
  if (src.empty()) {
    log::error("Specified shader file doesn't exist: {}", shader_path);
    return nullptr;
  }

  // Compile shader to SPIR-V
  log::info("Compiling shader: {}", shader_path);
  shaderc::Compiler compiler;
  shaderc::CompileOptions opts;
  // This version of shaderc does not include shaderc_env_version_vulkan_1_4, so
  // I use 1_3 instead. This does not matter since Vulkan 1.4 did not add any
  // new features to SPIR-V. And I will be switching this for Slang anyways
  // further down the road.
  opts.SetTargetEnvironment(shaderc_target_env_vulkan,
                            shaderc_env_version_vulkan_1_3);
  opts.SetTargetSpirv(shaderc_spirv_version_1_6);
  opts.SetOptimizationLevel(shaderc_optimization_level_performance);
  shaderc::CompilationResult result =
      compiler.CompileGlslToSpv(src, kind, file_name.c_str(), opts);

  if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
    log::error("Shader Compilation Error: {}", result.GetErrorMessage());
    return nullptr;
  }

  const size_t shader_size =
      (result.end() - result.cbegin()) * sizeof(uint32_t);

  VkShaderModuleCreateInfo module_create_info{
      .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
      .codeSize = shader_size,
      .pCode = result.cbegin()};

  VkShaderModule shader_module = VK_NULL_HANDLE;
  if (!VK_CHECK(vkCreateShaderModule(device, &module_create_info, nullptr,
                                     &shader_module))) {
    return nullptr;
  }
  return shader_module;
}

struct PushConstants {
  glm::mat4 transform = glm::mat4(1.0f);
  glm::mat4 view_projection = glm::mat4(1.0f);
  VkDeviceAddress vertex_buffer = 0;
  glm::vec4 color = glm::vec4(1.0f);
};

static_assert(sizeof(PushConstants) == 152);
static_assert(offsetof(PushConstants, transform) == 0);
static_assert(offsetof(PushConstants, view_projection) == 64);
static_assert(offsetof(PushConstants, vertex_buffer) == 128);
static_assert(offsetof(PushConstants, color) == 136);

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

  // No swapchain means the window starts minimized. resize() creates the
  // depth buffer once the swapchain exists.
  if (swapchain_->handle() && !createDepthBuffer(swapchain_->extent())) {
    log::error("Failed to create depth buffer");
    return false;
  }
  if (!createShaders()) {
    log::error("Failed to create shaders");
    return false;
  }
  if (pipeline_ = createGraphicsPipeline(); !pipeline_) {
    log::error("Unable to initialize the graphics pipeline");
    return false;
  }
  return true;
}

void Renderer::shutdown() {
  if (!device_) {
    return;
  }

  const VkDevice device = device_->logicalDevice();
  if (pipeline_layout_) {
    frame_ring_->deferDestroy([device, handle = pipeline_layout_] {
      vkDestroyPipelineLayout(device, handle, nullptr);
    });
    pipeline_layout_ = VK_NULL_HANDLE;
  }
  if (pipeline_) {
    frame_ring_->deferDestroy([device, handle = pipeline_] {
      vkDestroyPipeline(device, handle, nullptr);
    });
    pipeline_ = VK_NULL_HANDLE;
  }

  for (const Mesh &mesh : meshes_) {
    deferDestroyMesh(mesh);
  }
  meshes_.clear();

  if (vert_shader_) {
    frame_ring_->deferDestroy([device, handle = vert_shader_] {
      vkDestroyShaderModule(device, handle, nullptr);
    });
    vert_shader_ = VK_NULL_HANDLE;
  }
  if (frag_shader_) {
    frame_ring_->deferDestroy([device, handle = frag_shader_] {
      vkDestroyShaderModule(device, handle, nullptr);
    });
    frag_shader_ = VK_NULL_HANDLE;
  }

  destroyDepthBuffer();
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

bool Renderer::createShaders() {
  const VkDevice device = device_->logicalDevice();
  if (vert_shader_ =
          createShaderModule(device, "shader.vert", shaderc_vertex_shader);
      !vert_shader_) {
    return false;
  }
  if (frag_shader_ =
          createShaderModule(device, "shader.frag", shaderc_fragment_shader);
      !frag_shader_) {
    return false;
  }
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

void Renderer::deferDestroyMesh(const Mesh &mesh) {
  frame_ring_->deferDestroy([gfx_device = device_, mesh] {
    gfx::destroyBuffer(*gfx_device, mesh.vertex_buffer);
    gfx::destroyBuffer(*gfx_device, mesh.index_buffer);
  });
}

VkPipeline Renderer::createGraphicsPipeline() {
  VkPushConstantRange push_constant_range{.stageFlags =
                                              gfx::kPushConstantStages,
                                          .offset = 0,
                                          .size = sizeof(PushConstants)};

  VkPipelineLayoutCreateInfo pipeline_layout_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 0,
      .pushConstantRangeCount = 1,
      .pPushConstantRanges = &push_constant_range};
  if (!VK_CHECK(vkCreatePipelineLayout(device_->logicalDevice(),
                                       &pipeline_layout_info, nullptr,
                                       &pipeline_layout_))) {
    return nullptr;
  }

  const char *entry_point = "main";
  std::vector<VkPipelineShaderStageCreateInfo> shader_stages{
      {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
       .stage = VK_SHADER_STAGE_VERTEX_BIT,
       .module = vert_shader_,
       .pName = entry_point},
      {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
       .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
       .module = frag_shader_,
       .pName = entry_point}};

  // Vertex pulling
  VkPipelineVertexInputStateCreateInfo vert_input_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
  };

  // Input assembly
  VkPipelineInputAssemblyStateCreateInfo input_assembly_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};

  // Reverse-Z: nearer fragments have greater depth
  VkPipelineDepthStencilStateCreateInfo depth_stencil_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
      .depthTestEnable = VK_TRUE,
      .depthWriteEnable = VK_TRUE,
      .depthCompareOp = VK_COMPARE_OP_GREATER,
      .stencilTestEnable = VK_FALSE};

  VkPipelineViewportStateCreateInfo viewport_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .viewportCount = 1,
      .pViewports = nullptr,
      .scissorCount = 1,
      .pScissors = nullptr};

  VkPipelineRasterizationStateCreateInfo raster_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .polygonMode = VK_POLYGON_MODE_FILL,
      .cullMode = VK_CULL_MODE_BACK_BIT,
      .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
      .lineWidth = 1.0f,
  };

  VkPipelineMultisampleStateCreateInfo multi_sample_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};

  VkPipelineColorBlendAttachmentState attach_state{
      .blendEnable = VK_FALSE,
      .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT};

  VkPipelineColorBlendStateCreateInfo blend_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .attachmentCount = 1,
      .pAttachments = &attach_state};

  std::vector<VkDynamicState> dynamic_state{VK_DYNAMIC_STATE_VIEWPORT,
                                            VK_DYNAMIC_STATE_SCISSOR};

  VkPipelineDynamicStateCreateInfo dynamic_state_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
      .dynamicStateCount = static_cast<uint32_t>(dynamic_state.size()),
      .pDynamicStates = dynamic_state.data()};

  const VkFormat color_format = swapchain_->format();

  VkPipelineRenderingCreateInfo render_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
      .colorAttachmentCount = 1,
      .pColorAttachmentFormats = &color_format,
      .depthAttachmentFormat = kDepthFormat};

  VkGraphicsPipelineCreateInfo pipeline_info{
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .pNext = &render_info,
      .stageCount = static_cast<uint32_t>(shader_stages.size()),
      .pStages = shader_stages.data(),
      .pVertexInputState = &vert_input_info,
      .pInputAssemblyState = &input_assembly_info,
      .pViewportState = &viewport_info,
      .pRasterizationState = &raster_info,
      .pMultisampleState = &multi_sample_info,
      .pDepthStencilState = &depth_stencil_info,
      .pColorBlendState = &blend_info,
      .pDynamicState = &dynamic_state_info,
      .layout = pipeline_layout_,
      .renderPass = VK_NULL_HANDLE,
  };

  VkPipeline new_pipeline = VK_NULL_HANDLE;
  if (!VK_CHECK(vkCreateGraphicsPipelines(device_->logicalDevice(), nullptr, 1,
                                          &pipeline_info, nullptr,
                                          &new_pipeline))) {
    return nullptr;
  }

  return new_pipeline;
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
    cmd.bindPipeline(pipeline_, pipeline_layout_);

    for (const DrawItem &draw : desc.draws) {
      assert(draw.mesh.index < meshes_.size() && "DrawItem has no valid mesh");
      if (draw.mesh.index >= meshes_.size()) {
        continue;
      }
      const Mesh &mesh = meshes_[draw.mesh.index];

      cmd.bindIndexBuffer(mesh.index_buffer);
      cmd.pushConstants(PushConstants{
          .transform = draw.transform,
          .view_projection = desc.view_projection,
          .vertex_buffer = mesh.vertex_buffer.address,
          .color = draw.color});
      cmd.drawIndexed(mesh.index_count);
    }
  }
  cmd.endRendering();

  cmd.transition({.image = swapchain_->image(ctx.image_index),
                  .from = gfx::ResourceState::ColorTarget,
                  .to = gfx::ResourceState::Present});
}

} // namespace strafe::renderer
