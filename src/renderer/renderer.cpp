#include "renderer/renderer.h"

#include "core/log.h"
#include "core/utils.h"
#include "gfx/buffer.h"
#include "gfx/device.h"
#include "gfx/framering.h"
#include "gfx/swapchain.h"
#include "gfx/vk_check.h"

#include "vulkan/vulkan_core.h"
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

struct Vertex {
  glm::vec3 position;
  glm::vec3 color;
};

static_assert(sizeof(Vertex) == 24);
static_assert(offsetof(Vertex, position) == 0);
static_assert(offsetof(Vertex, color) == 12);
static_assert(std::is_trivially_copyable_v<Vertex>);

struct PushConstants {
  glm::mat4 transform = glm::mat4(1.0f);
  VkDeviceAddress vertex_buffer = 0;
  glm::vec4 color = glm::vec4(1.0f);
};

static_assert(sizeof(PushConstants) == 88);
static_assert(offsetof(PushConstants, transform) == 0);
static_assert(offsetof(PushConstants, vertex_buffer) == 64);
static_assert(offsetof(PushConstants, color) == 72);

constexpr std::array<Vertex, 3> kTriangleVerts{
    Vertex{.position = {-0.5f, -0.5f, 0.0f}, .color = {1.0f, 1.0f, 1.0f}},
    Vertex{.position = {-0.5f, 0.5f, 0.0f}, .color = {1.0f, 1.0f, 1.0f}},
    Vertex{.position = {0.5f, 0.5f, 0.0f}, .color = {1.0f, 1.0f, 1.0f}}};

constexpr VkShaderStageFlags kPushConstantStages =
    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

} // namespace

bool Renderer::initialize(gfx::Device &device, gfx::Swapchain &swapchain,
                          gfx::FrameRing &frame_ring) {
  device_ = &device;
  swapchain_ = &swapchain;
  frame_ring_ = &frame_ring;

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
  if (!createVertexBuffer()) {
    log::error("Failed to create vertex buffer");
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

  if (vertex_buffer_.handle) {
    frame_ring_->deferDestroy([device = device_, buffer = vertex_buffer_] {
      gfx::destroyBuffer(*device, buffer);
    });
    vertex_buffer_ = {};
  }

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
}

bool Renderer::resize(VkExtent2D extent) {
  destroyDepthBuffer();
  if (extent.width == 0 || extent.height == 0) {
    return true;
  }
  return createDepthBuffer(extent);
}

bool Renderer::createDepthBuffer(VkExtent2D extent) {
  VkImageCreateInfo depth_create_info{
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .imageType = VK_IMAGE_TYPE_2D,
      .format = kDepthFormat,
      .extent{.width = extent.width, .height = extent.height, .depth = 1},
      .mipLevels = 1,
      .arrayLayers = 1,
      .samples = VK_SAMPLE_COUNT_1_BIT,
      .tiling = VK_IMAGE_TILING_OPTIMAL,
      .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
      .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};

  VmaAllocationCreateInfo alloc_info{
      .flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
      .usage = VMA_MEMORY_USAGE_AUTO};
  if (!VK_CHECK(vmaCreateImage(device_->allocator(), &depth_create_info,
                               &alloc_info, &depth_image_,
                               &depth_image_allocation_, nullptr))) {
    return false;
  }

  VkImageViewCreateInfo depth_img_view_info{
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = depth_image_,
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .format = kDepthFormat,
      .subresourceRange{.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                        .levelCount = 1,
                        .layerCount = 1}};
  if (!VK_CHECK(vkCreateImageView(device_->logicalDevice(),
                                  &depth_img_view_info, nullptr,
                                  &depth_image_view_))) {
    return false;
  }

  return true;
}

void Renderer::destroyDepthBuffer() {
  if (depth_image_view_) {
    vkDestroyImageView(device_->logicalDevice(), depth_image_view_, nullptr);
    depth_image_view_ = VK_NULL_HANDLE;
  }
  if (depth_image_) {
    vmaDestroyImage(device_->allocator(), depth_image_,
                    depth_image_allocation_);
    depth_image_ = VK_NULL_HANDLE;
    depth_image_allocation_ = VK_NULL_HANDLE;
  }
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

bool Renderer::createVertexBuffer() {
  const std::span<const std::byte> bytes =
      std::as_bytes(std::span(kTriangleVerts));

  auto new_buffer = gfx::createBuffer(*device_, bytes.size(), 0);

  if (!new_buffer) {
    log::error("Failed to create vertex buffer");
    return false;
  }

  vertex_buffer_ = *new_buffer;

  return gfx::writeBuffer(*device_, vertex_buffer_, bytes);
}

VkPipeline Renderer::createGraphicsPipeline() {
  VkPushConstantRange push_constant_range{.stageFlags = kPushConstantStages,
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

  // Depth stencil configuration
  VkPipelineDepthStencilStateCreateInfo depth_stencil_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
      .depthTestEnable = VK_TRUE,
      .depthWriteEnable = VK_TRUE,
      .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
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

  // Transition the color and depth images
  std::vector<VkImageMemoryBarrier2> layout_barriers{
      {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
       .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
       .srcAccessMask = 0,
       .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
       .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
       .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
       .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
       .image = swapchain_->image(ctx.image_index),
       .subresourceRange{
           .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
           .baseMipLevel = 0,
           .levelCount = 1,
           .baseArrayLayer = 0,
           .layerCount = 1,
       }},
      {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
       .srcStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                       VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
       .srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
       .dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                       VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
       .dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
       .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
       .newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
       .image = depth_image_,
       .subresourceRange{
           .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
           .baseMipLevel = 0,
           .levelCount = 1,
           .baseArrayLayer = 0,
           .layerCount = 1,
       }}};

  VkDependencyInfo dep_info{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                            .imageMemoryBarrierCount =
                                static_cast<uint32_t>(layout_barriers.size()),
                            .pImageMemoryBarriers = layout_barriers.data()};
  vkCmdPipelineBarrier2(ctx.cmd, &dep_info);

  VkRenderingAttachmentInfo color_attachment_info{
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .imageView = swapchain_->imageView(ctx.image_index),
      .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,   // Clear the image
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE, // keep dAta for presentation
      .clearValue{.color{0.01f, 0.01f, 0.01f, 1}}};
  VkRenderingAttachmentInfo depth_attachment_info{
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .imageView = depth_image_view_,
      .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
      .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
      .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
      .clearValue{.depthStencil{.depth = 1.0f, .stencil = 0}}};

  VkRenderingInfo rendering_info{
      .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
      .renderArea{.offset{.x = 0, .y = 0}, .extent = extent},
      .layerCount = 1,
      .colorAttachmentCount = 1,
      .pColorAttachments = &color_attachment_info,
      .pDepthAttachment = &depth_attachment_info};

  // Begin dynamic rendering
  vkCmdBeginRendering(ctx.cmd, &rendering_info);
  {
    VkViewport viewport{
        .x = 0,
        .y = 0,
        .width = static_cast<float>(extent.width),
        .height = static_cast<float>(extent.height),
        .minDepth = 0,
        .maxDepth = 1,
    };
    vkCmdSetViewport(ctx.cmd, 0, 1, &viewport);

    VkRect2D scissor{.offset{.x = 0, .y = 0}, .extent = extent};
    vkCmdSetScissor(ctx.cmd, 0, 1, &scissor);

    vkCmdBindPipeline(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);

    for (const auto &draw : desc.draws) {
      const PushConstants push{.transform = draw.transform,
                               .vertex_buffer = vertex_buffer_.address,
                               .color = draw.color};
      vkCmdPushConstants(ctx.cmd, pipeline_layout_, kPushConstantStages, 0,
                         sizeof(push), &push);
      vkCmdDraw(ctx.cmd, static_cast<uint32_t>(kTriangleVerts.size()), 1, 0, 0);
    }
  }
  // End dynamic rendering
  vkCmdEndRendering(ctx.cmd);

  VkImageMemoryBarrier2 present_layout_barrier{
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
      .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
      .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
      .dstStageMask = VK_PIPELINE_STAGE_2_NONE,
      .dstAccessMask = 0,
      .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
      .image = swapchain_->image(ctx.image_index),
      .subresourceRange{
          .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
          .baseMipLevel = 0,
          .levelCount = 1,
          .baseArrayLayer = 0,
          .layerCount = 1,
      }};

  VkDependencyInfo present_dep_info{.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                                    .imageMemoryBarrierCount = 1,
                                    .pImageMemoryBarriers =
                                        &present_layout_barrier};
  vkCmdPipelineBarrier2(ctx.cmd, &present_dep_info);
}

} // namespace strafe::renderer
