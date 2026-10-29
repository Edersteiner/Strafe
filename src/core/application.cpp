#include "core/application.h"

#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

#include "core/log.h"
#include "core/utils.h"
#include "core/flags.h"

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
  if (!swapchain_.initialize(device_, VkExtent2D{static_cast<uint32_t>(width_), static_cast<uint32_t>(height_)})) {
    log::error("Failed to create swapchain");
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
  if (!createSyncResources()) {
    log::error("Couldn't create the sync related resources");
    return false;
  }
  if (!createCommandBuffers()) {
    log::error("Couldn't create command buffer objects");
    return false;
  }
  return true;
}

bool Application::createDepthBuffer() {
  const VkExtent2D extent = swapchain_.extent();
  VkImageCreateInfo depth_create_info {
    .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
    .imageType = VK_IMAGE_TYPE_2D,
    .format = depth_format_,
    .extent{.width = extent.width, .height = extent.height, .depth = 1 },
    .mipLevels = 1,
    .arrayLayers = 1,
    .samples = VK_SAMPLE_COUNT_1_BIT,
    .tiling = VK_IMAGE_TILING_OPTIMAL,
    .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
  };

  VmaAllocationCreateInfo alloc_info {
    .flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT,
    .usage = VMA_MEMORY_USAGE_AUTO
  };
  if (vmaCreateImage(device_.allocator(), &depth_create_info, &alloc_info, &depth_image_, &depth_image_allocation_, nullptr) != VK_SUCCESS) {
    log::error("Error allocating depth image");
    return false;
  }

  VkImageViewCreateInfo depth_img_view_info {
    .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
    .image = depth_image_,
    .viewType = VK_IMAGE_VIEW_TYPE_2D,
    .format = depth_format_,
    .subresourceRange{.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT, .levelCount = 1, .layerCount = 1}
  };
  if (vkCreateImageView(device_.logicalDevice(), &depth_img_view_info, nullptr, &depth_image_view_) != VK_SUCCESS) {
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
  if (vert_shader_ = createShaderModule("shader.vert", shaderc_vertex_shader); !vert_shader_) {
    return false;
  }
  if (frag_shader_ = createShaderModule("shader.frag", shaderc_fragment_shader); !frag_shader_) {
    return false;
  }
  return true;
}

VkShaderModule Application::createShaderModule(const std::string &file_name, shaderc_shader_kind kind) const {
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
  // This version of shaderc does not include shaderc_env_version_vulkan_1_4, so I use 1_3 instead.
  // This does not matter since Vulkan 1.4 did not add any new features to SPIR-V.
  // And I will be switching this for Slang anyways further down the road.
  opts.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);
  opts.SetTargetSpirv(shaderc_spirv_version_1_6);
  opts.SetOptimizationLevel(shaderc_optimization_level_performance);
  shaderc::CompilationResult result = compiler.CompileGlslToSpv(src, kind, file_name.c_str(), opts);

  if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
    log::error("Shader Compilation Error: {}", result.GetErrorMessage());
    return nullptr;
  }

  const size_t shader_size = (result.end() - result.cbegin()) * sizeof(uint32_t);

  VkShaderModuleCreateInfo module_create_info {
    .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    .codeSize = shader_size,
    .pCode = result.cbegin()
  };

  VkShaderModule shader_module = VK_NULL_HANDLE;
  if (vkCreateShaderModule(device_.logicalDevice(), &module_create_info, nullptr, &shader_module) != VK_SUCCESS) {
    log::error("Error creating shader modulke");
    return nullptr;
  }
  return shader_module;
}

VkPipeline Application::createGraphicsPipeline() {
  VkPipelineLayoutCreateInfo pipeline_layout_info {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
    .setLayoutCount = 0,
    .pushConstantRangeCount = 0
  };
  if (vkCreatePipelineLayout(device_.logicalDevice(), &pipeline_layout_info, nullptr, &pipeline_layout_) != VK_SUCCESS) {
    log::info("Failed to create pipeline layout");
    return nullptr;
  }

  const char *entry_point = "main";
  std::vector<VkPipelineShaderStageCreateInfo> shader_stages {
    {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_VERTEX_BIT,
      .module = vert_shader_,
      .pName = entry_point
    },
    {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
      .module = frag_shader_,
      .pName = entry_point
    }
  };

  // Vertex pulling
  VkPipelineVertexInputStateCreateInfo vert_input_info {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
  };

  // Input assembly
  VkPipelineInputAssemblyStateCreateInfo input_assembly_info {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
    .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
  };

  // Depth stencil configuration
  VkPipelineDepthStencilStateCreateInfo depth_stencil_info {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
    .depthTestEnable = VK_TRUE,
    .depthWriteEnable = VK_TRUE,
    .depthCompareOp = VK_COMPARE_OP_LESS,
    .stencilTestEnable = VK_FALSE
  };

  VkPipelineViewportStateCreateInfo viewport_info {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
    .viewportCount = 1,
    .pViewports = nullptr,
    .scissorCount = 1,
    .pScissors = nullptr
  };

  VkPipelineRasterizationStateCreateInfo raster_info {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
    .polygonMode = VK_POLYGON_MODE_FILL,
    .cullMode = VK_CULL_MODE_BACK_BIT,
    .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
    .lineWidth = 1.0f,
  };

  VkPipelineMultisampleStateCreateInfo multi_sample_info {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
    .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
  };

  VkPipelineColorBlendAttachmentState attach_state {
    .blendEnable = VK_FALSE,
    .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
  };

  VkPipelineColorBlendStateCreateInfo blend_info {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
    .attachmentCount = 1,
    .pAttachments = &attach_state
  };

  std::vector<VkDynamicState> dynamic_state {
    VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR
  };

  VkPipelineDynamicStateCreateInfo dynamic_state_info {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
    .dynamicStateCount = static_cast<uint32_t>(dynamic_state.size()),
    .pDynamicStates = dynamic_state.data()
  };

  const VkFormat color_format = swapchain_.format();

  VkPipelineRenderingCreateInfo render_info {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
    .colorAttachmentCount = 1,
    .pColorAttachmentFormats = &color_format,
    .depthAttachmentFormat = depth_format_
  };

  VkGraphicsPipelineCreateInfo pipeline_info {
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
  if (vkCreateGraphicsPipelines(device_.logicalDevice(), nullptr, 1, &pipeline_info, nullptr, &new_pipeline) != VK_SUCCESS) {
    log::error("Error creating the pipeline");
    return nullptr;
  }

  return new_pipeline;
}

bool Application::createSyncResources() {
  VkSemaphoreTypeCreateInfo semaphore_type_info {
    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
    .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
    .initialValue = kMaxFramesInFlight
  };
  VkSemaphoreCreateInfo semaphore_info {
    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
    .pNext = &semaphore_type_info
  };
  if (vkCreateSemaphore(device_.logicalDevice(), &semaphore_info, nullptr, &timeline_semaphore) != VK_SUCCESS) {
    log::error("Failed to create timeline semaphore");
    return false;
  }

  for (FrameResources &res : frame_resources_) {
    VkSemaphoreCreateInfo semaphore_info{ .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    if (vkCreateSemaphore(device_.logicalDevice(), &semaphore_info, nullptr, &res.image_acquired_semaphore) != VK_SUCCESS) {
      log::error("Error creating the per-frame image-acquire semaphore");
      return false;
    }
  }

  return true;
}

bool Application::createCommandBuffers() {
  for (FrameResources &res : frame_resources_) {
    VkCommandPoolCreateInfo pool_info {
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .queueFamilyIndex = device_.graphicsQueueFamily()
    };
    if (vkCreateCommandPool(device_.logicalDevice(), &pool_info, nullptr, &res.command_pool) != VK_SUCCESS) {
      log::error("Unable to create command buffer pool");
      return false;
    }

    VkCommandBufferAllocateInfo cmd_alloc_info {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = res.command_pool,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1,
    };

    if (vkAllocateCommandBuffers(device_.logicalDevice(), &cmd_alloc_info, &res.command_buffer) != VK_SUCCESS) {
      log::error("Unable to allocate command buffer");
      return false;
    }
  }
  return true;
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
        require_swapchain_recreate = true;
      }
    }
    render();
  }
}

void Application::render() {
  if (require_swapchain_recreate) {
    if (!swapchain_.recreate(VkExtent2D{static_cast<uint32_t>(width_), static_cast<uint32_t>(height_)})) {
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

    require_swapchain_recreate = (swapchain_.handle() == VK_NULL_HANDLE);
  }

  // No swapchain (window minimized, etc.) skip rendering.
  if (swapchain_.handle() == VK_NULL_HANDLE) {
    return;
  }

  const uint32_t frame_res_index = frame_index_ % kMaxFramesInFlight;
  const uint64_t signal_value = next_signal_value;
  const uint64_t wait_value = signal_value - kMaxFramesInFlight;

  VkSemaphoreWaitInfo wait_info {
    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
    .semaphoreCount = 1,
    .pSemaphores = &timeline_semaphore,
    .pValues = &wait_value
  };
  vkWaitSemaphores(device_.logicalDevice(), &wait_info, UINT64_MAX);

  FrameResources &res = frame_resources_[frame_res_index];
  vkResetCommandPool(device_.logicalDevice(), res.command_pool, 0);

  VkSemaphore image_acquire_semaphore = frame_resources_[frame_res_index].image_acquired_semaphore;

  uint32_t image_index = 0;
  VkResult acquire_result = vkAcquireNextImageKHR(device_.logicalDevice(), swapchain_.handle(), UINT64_MAX, image_acquire_semaphore, VK_NULL_HANDLE, &image_index);

  if (acquire_result == VK_ERROR_OUT_OF_DATE_KHR) {
    require_swapchain_recreate = true;
    return;
  }
  else if (acquire_result == VK_SUBOPTIMAL_KHR) {
    require_swapchain_recreate = true;
  }

  ++frame_index_;
  ++next_signal_value;

  VkCommandBufferBeginInfo cmd_begin_info {
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
  };

  vkBeginCommandBuffer(res.command_buffer, &cmd_begin_info);

  // Transition the color and depth images
  std::vector<VkImageMemoryBarrier2> layout_barriers {
    {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
      .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
      .srcAccessMask = 0,
      .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
      .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
      .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
      .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      .image = swapchain_.image(image_index),
      .subresourceRange {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1,
      }
    },
    {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
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
      .subresourceRange {
        .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1,
      }
    }
  };

  VkDependencyInfo dep_info {
    .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
    .imageMemoryBarrierCount = static_cast<uint32_t>(layout_barriers.size()),
    .pImageMemoryBarriers = layout_barriers.data()
  };
  vkCmdPipelineBarrier2(res.command_buffer, &dep_info);

  VkRenderingAttachmentInfo color_attachment_info {
    .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
    .imageView = swapchain_.imageView(image_index),
    .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, // Clear the image
    .storeOp = VK_ATTACHMENT_STORE_OP_STORE, // keep dAta for presentation
    .clearValue{.color{0.01f, 0.01f, 0.01f, 1}}
  };
  VkRenderingAttachmentInfo depth_attachment_info {
    .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
    .imageView = depth_image_view_,
    .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
    .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
    .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
    .clearValue{.depthStencil{1.0f, 0}}
  };

  VkRenderingInfo rendering_info {
    .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
    .renderArea {
      .offset{.x = 0, .y = 0},
      .extent = swapchain_.extent()
      },
    .layerCount = 1,
    .colorAttachmentCount = 1,
    .pColorAttachments = &color_attachment_info,
    .pDepthAttachment = &depth_attachment_info
  };

  // Begin dynamic rendering
  vkCmdBeginRendering(res.command_buffer, &rendering_info);
  {
    VkViewport viewport
    {
      .x = 0, .y = 0,
      .width = static_cast<float>(swapchain_.extent().width),
      .height = static_cast<float>(swapchain_.extent().height),
      .minDepth = 0,
      .maxDepth = 1,
    };
    vkCmdSetViewport(res.command_buffer, 0, 1, &viewport);

    VkRect2D scissor {
      .offset{.x = 0, .y = 0},
      .extent = swapchain_.extent()
    };
    vkCmdSetScissor(res.command_buffer, 0, 1, &scissor);

    vkCmdBindPipeline(res.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdDraw(res.command_buffer, 3, 1, 0, 0);
  }
  // End dynamic rendering
  vkCmdEndRendering(res.command_buffer);

  VkImageMemoryBarrier2 present_layout_barrier {
    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
    .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
    .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
    .dstStageMask = VK_PIPELINE_STAGE_2_NONE,
    .dstAccessMask = 0,
    .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    .image = swapchain_.image(image_index),
    .subresourceRange {
      .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
      .baseMipLevel = 0,
      .levelCount = 1,
      .baseArrayLayer = 0,
      .layerCount = 1,
    }
  };

  VkDependencyInfo present_dep_info {
    .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
    .imageMemoryBarrierCount = 1,
    .pImageMemoryBarriers = &present_layout_barrier
  };
  vkCmdPipelineBarrier2(res.command_buffer, &present_dep_info);

  vkEndCommandBuffer(res.command_buffer);

  VkSemaphoreSubmitInfo image_acquire_wait_info
  {
    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
    .semaphore = image_acquire_semaphore,
    .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
  };

  std::vector<VkSemaphoreSubmitInfo> semaphore_signals {
    { // render work completion signal
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
      .semaphore = swapchain_.presentSemaphore(image_index),
      .stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT
    },
    { // entire frame is completed (timeline)
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
      .semaphore = timeline_semaphore,
      .value = signal_value,
      .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT
    }
  };
  VkCommandBufferSubmitInfo cmd_submit_info {
    .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
    .commandBuffer = res.command_buffer,
  };
  VkSubmitInfo2 submit_info {
    .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
    .waitSemaphoreInfoCount = 1,
    .pWaitSemaphoreInfos = &image_acquire_wait_info,
    .commandBufferInfoCount = 1,
    .pCommandBufferInfos = &cmd_submit_info,
    .signalSemaphoreInfoCount = static_cast<uint32_t>(semaphore_signals.size()),
    .pSignalSemaphoreInfos = semaphore_signals.data()
  };
  vkQueueSubmit2(device_.graphicsQueue(), 1, &submit_info, VK_NULL_HANDLE);

  const VkSemaphore present_semaphor = swapchain_.presentSemaphore(image_index);
  const VkSwapchainKHR swapchain_handle = swapchain_.handle();

  VkPresentInfoKHR present_info {
    .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
    .waitSemaphoreCount = 1,
    .pWaitSemaphores = &present_semaphor,
    .swapchainCount = 1,
    .pSwapchains = &swapchain_handle,
    .pImageIndices = &image_index,
    .pResults = nullptr
  };

  vkQueuePresentKHR(device_.graphicsQueue(), &present_info);
}

void Application::shutdown() {
  if (device_.logicalDevice()) vkDeviceWaitIdle(device_.logicalDevice());

  if (timeline_semaphore) {
    vkDestroySemaphore(device_.logicalDevice(), timeline_semaphore, nullptr);
  }
  for (auto &res : frame_resources_) {
    vkDestroySemaphore(device_.logicalDevice(), res.image_acquired_semaphore, nullptr);
    vkDestroyCommandPool(device_.logicalDevice(), res.command_pool, nullptr); // Implicitly destroys buffers too
  }
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

  destroyDepthBuffer();
  swapchain_.shutdown();
  device_.shutdown();

  if (window_) {
    SDL_DestroyWindow(window_);
  }
  SDL_Quit();
}

} // namespace strafe
