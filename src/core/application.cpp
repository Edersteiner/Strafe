#include "core/application.h"

#include <SDL3/SDL_vulkan.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "core/log.h"
#include "core/utils.h"

// Todo: actually abstract the rendering and not write everything in one file.
// Coming soon I promise.
//
// Update: In progress :^)

namespace strafe {

bool Application::initialize() {
  if (SDL_InitSubSystem(SDL_INIT_VIDEO)) {
    window_ = SDL_CreateWindow("Strafe", width_, height_,
                               SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);

    if (!window_) {
      log::error("Failed to create window: {}", SDL_GetError());
      return false;
    }

    SDL_GetWindowSizeInPixels(window_, &width_, &height_);

    if (!initializeVulkan()) {
      log::error("Failed to initialize Vulkan");
      return false;
    }
  } else {
    log::error("Failed to initialise SDL Subsystems: {}", SDL_GetError());
    return false;
  }

  return true;
}

bool Application::initializeVulkan() {
  if (!device_.initialize(window_, swapchain_.format())) {
    log::error("Failed to initialize the GPU device");
    return false;
  }
  if (!swapchain_.initialize(device_,
                             VkExtent2D{static_cast<uint32_t>(width_),
                                        static_cast<uint32_t>(height_)})) {
    log::error("Failed to create swapchain");
    return false;
  }
  if (!frame_ring_.initialize(device_)) {
    log::error("Failed to initialize frame ring");
    return false;
  }

  // No swapchain means the window starts minimized. render() creates the
  // depth buffer once the swapchain exists.
  if (swapchain_.handle() && !createDepthBuffer()) {
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

bool Application::createDepthBuffer() {
  const VkExtent2D extent = swapchain_.extent();
  VkImageCreateInfo depth_create_info{
      .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
      .imageType = VK_IMAGE_TYPE_2D,
      .format = depth_format_,
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
  if (vmaCreateImage(device_.allocator(), &depth_create_info, &alloc_info,
                     &depth_image_, &depth_image_allocation_,
                     nullptr) != VK_SUCCESS) {
    log::error("Error allocating depth image");
    return false;
  }

  VkImageViewCreateInfo depth_img_view_info{
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = depth_image_,
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .format = depth_format_,
      .subresourceRange{.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                        .levelCount = 1,
                        .layerCount = 1}};
  if (vkCreateImageView(device_.logicalDevice(), &depth_img_view_info, nullptr,
                        &depth_image_view_) != VK_SUCCESS) {
    log::error("Error creating depth image view");
    return false;
  }

  return true;
}

void Application::destroyDepthBuffer() {
  if (depth_image_view_) {
    vkDestroyImageView(device_.logicalDevice(), depth_image_view_, nullptr);
    depth_image_view_ = VK_NULL_HANDLE;
  }
  if (depth_image_) {
    vmaDestroyImage(device_.allocator(), depth_image_, depth_image_allocation_);
    depth_image_ = VK_NULL_HANDLE;
    depth_image_allocation_ = VK_NULL_HANDLE;
  }
}

bool Application::createShaders() {
  if (vert_shader_ = createShaderModule("shader.vert", shaderc_vertex_shader);
      !vert_shader_) {
    return false;
  }
  if (frag_shader_ = createShaderModule("shader.frag", shaderc_fragment_shader);
      !frag_shader_) {
    return false;
  }
  return true;
}

VkShaderModule Application::createShaderModule(const std::string &file_name,
                                               shaderc_shader_kind kind) const {
  // Read shader file from disk
  const std::string shaderPath = utils::basePath() + "shaders/" + file_name;
  const std::string src = utils::readTextFile(shaderPath);
  if (src.empty()) {
    log::error("Specified shader file doesn't exist: {}", shaderPath);
    return nullptr;
  }

  // Compile shader to SPIR-V
  log::info("Compiling shader: {}", shaderPath);
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
  if (vkCreateShaderModule(device_.logicalDevice(), &module_create_info,
                           nullptr, &shader_module) != VK_SUCCESS) {
    log::error("Error creating shader modulke");
    return nullptr;
  }
  return shader_module;
}

VkPipeline Application::createGraphicsPipeline() {
  VkPipelineLayoutCreateInfo pipeline_layout_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 0,
      .pushConstantRangeCount = 0};
  if (vkCreatePipelineLayout(device_.logicalDevice(), &pipeline_layout_info,
                             nullptr, &pipeline_layout_) != VK_SUCCESS) {
    log::info("Failed to create pipeline layout");
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
      .depthCompareOp = VK_COMPARE_OP_LESS,
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

  const VkFormat color_format = swapchain_.format();

  VkPipelineRenderingCreateInfo render_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
      .colorAttachmentCount = 1,
      .pColorAttachmentFormats = &color_format,
      .depthAttachmentFormat = depth_format_};

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
  if (vkCreateGraphicsPipelines(device_.logicalDevice(), nullptr, 1,
                                &pipeline_info, nullptr,
                                &new_pipeline) != VK_SUCCESS) {
    log::error("Error creating the pipeline");
    return nullptr;
  }

  return new_pipeline;
}

void Application::run() {
  running_ = true;
  while (running_) {
    SDL_Event event{0};
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_EVENT_QUIT) {
        running_ = false;
        break;
      } else if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
        SDL_GetWindowSizeInPixels(window_, &width_, &height_);
        swapchain_.markForRecreate();
      }
    }
    render();
  }
}

void Application::render() {
  if (swapchain_.requireRecreate()) {
    if (!swapchain_.recreate(VkExtent2D{static_cast<uint32_t>(width_),
                                        static_cast<uint32_t>(height_)})) {
      log::error("Cannot recreate swapchain!");
      running_ = false;
      return;
    }

    destroyDepthBuffer();
    if (swapchain_.handle() && !createDepthBuffer()) {
      log::error("Cannot recreate depth buffer!");
      running_ = false;
      return;
    }
  }

  std::optional<gfx::FrameContext> frame = frame_ring_.beginFrame(swapchain_);
  if (!frame) {
    return;
  }

  const gfx::FrameContext &ctx = *frame;

  // Transition the color and depth images
  std::vector<VkImageMemoryBarrier2> layout_barriers{
      {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
       .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
       .srcAccessMask = 0,
       .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
       .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
       .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
       .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
       .image = swapchain_.image(ctx.image_index),
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
      .imageView = swapchain_.imageView(ctx.image_index),
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
      .clearValue{.depthStencil{1.0f, 0}}};

  VkRenderingInfo rendering_info{
      .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
      .renderArea{.offset{.x = 0, .y = 0}, .extent = swapchain_.extent()},
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
        .width = static_cast<float>(swapchain_.extent().width),
        .height = static_cast<float>(swapchain_.extent().height),
        .minDepth = 0,
        .maxDepth = 1,
    };
    vkCmdSetViewport(ctx.cmd, 0, 1, &viewport);

    VkRect2D scissor{.offset{.x = 0, .y = 0}, .extent = swapchain_.extent()};
    vkCmdSetScissor(ctx.cmd, 0, 1, &scissor);

    vkCmdBindPipeline(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdDraw(ctx.cmd, 3, 1, 0, 0);
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
      .image = swapchain_.image(ctx.image_index),
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

  frame_ring_.endFrame(ctx, swapchain_);
}

void Application::shutdown() {
  if (device_.logicalDevice())
    vkDeviceWaitIdle(device_.logicalDevice());

  if (pipeline_layout_) {
    vkDestroyPipelineLayout(device_.logicalDevice(), pipeline_layout_, nullptr);
  }
  if (pipeline_) {
    vkDestroyPipeline(device_.logicalDevice(), pipeline_, nullptr);
  }

  if (vert_shader_) {
    vkDestroyShaderModule(device_.logicalDevice(), vert_shader_, nullptr);
  }
  if (frag_shader_) {
    vkDestroyShaderModule(device_.logicalDevice(), frag_shader_, nullptr);
  }

  frame_ring_.shutdown();
  destroyDepthBuffer();
  swapchain_.shutdown();
  device_.shutdown();

  if (window_) {
    SDL_DestroyWindow(window_);
  }
  SDL_Quit();
}

} // namespace strafe
