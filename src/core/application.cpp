#include "core/application.h"

#include <SDL3/SDL_vulkan.h>

#include <cstdint>
#include <vector>

#include "core/log.h"
#include "core/utils.h"
#include "vulkan/vulkan_core.h"

// Todo: actually abstract the rendering and not write everything in one file.
// Coming soon I promise.

namespace strafe {

VKAPI_ATTR VkBool32 VKAPI_CALL
debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
              VkDebugUtilsMessageTypeFlagsEXT message_typ,
              const VkDebugUtilsMessengerCallbackDataEXT *p_callback_data,
              void *p_user_data) {
  const char *type = "general";
  if (message_typ & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) {
    type = "validation";
  } else if (message_typ & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) {
    type = "performance";
  }

  const char *message = p_callback_data->pMessage;
  switch (message_severity) {
  case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
    log::trace("[vulkan:{}] {}", type, message);
    break;
  case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
    log::info("[vulkan:{}] {}", type, message);
    break;
  case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
    log::warn("[vulkan:{}] {}", type, message);
    break;
  case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
  default:
    log::error("[vulkan:{}] {}", type, message);
    break;
  }

  return VK_FALSE;
}

bool Application::initialize() {
  if (SDL_InitSubSystem(SDL_INIT_VIDEO)) {
    window_ = SDL_CreateWindow("Strafe", width_, height_,
                               SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);

    if (!window_) {
      log::error("Failed to create window: {}", SDL_GetError());
      return false;
    }

    if (!initializeVulkan()) {
      log::error("Failed to initialize Vulkan");
      return false;
    }
  }

  return true;
}

bool Application::initializeVulkan() {
  if (!createVulkanInstance()) {
    log::error("Couldn't create Vulkan instance");
    return false;
  }
  if (!createSurface()) {
    log::error("Couldn't create window surface");
    return false;
  }
  if (physical_device_ = findPhysicalDevice(); !physical_device_) {
    log::error("Failed to select physical device");
    return false;
  }
  if (!findGraphicsQueue()) {
    log::error("Failed to find a compatible graphics queue");
    return false;
  }
  if (!createDevice(physical_device_)) {
    log::error("Failed to create logical GPU device");
    return false;
  }
  if (!initializeVMA()) {
    log::error("Failed to initialize VMA");
    return false;
  }
  if (!createSwapchain(width_, height_)) {
    log::error("Failed to create swapchain");
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

bool Application::createVulkanInstance() {
  if (volkInitialize() != VK_SUCCESS) {
    log::error("Error initializing Volk");
    return false;
  }

  VkApplicationInfo app_info{
      .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pApplicationName = "Strafe Engine Application",
      .apiVersion = kVulkanVersion,
  };

  // Find required extensions for platform
  uint32_t inst_ext_count = 0;
  const char *const *extensions =
      SDL_Vulkan_GetInstanceExtensions(&inst_ext_count);
  std::vector<const char *> requested_extensions{
      VK_EXT_DEBUG_UTILS_EXTENSION_NAME};
  for (size_t i = 0; i < inst_ext_count; ++i) {
    requested_extensions.push_back(extensions[i]);
  }

  std::vector<const char *> requested_layers{"VK_LAYER_KHRONOS_validation"};

  VkDebugUtilsMessengerCreateInfoEXT debug_info{
      .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
      .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
      .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
      .pfnUserCallback = debugCallback};

  VkInstanceCreateInfo inst_create_info{
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
      .pNext = &debug_info,
      .pApplicationInfo = &app_info,
      .enabledLayerCount = static_cast<uint32_t>(requested_layers.size()),
      .ppEnabledLayerNames = requested_layers.data(),
      .enabledExtensionCount =
          static_cast<uint32_t>(requested_extensions.size()),
      .ppEnabledExtensionNames = requested_extensions.data(),
  };

  if (vkCreateInstance(&inst_create_info, nullptr, &vulkan_instance_) !=
      VK_SUCCESS) {
    return false;
  }

  volkLoadInstance(vulkan_instance_);

  return true;
}

bool Application::createSurface() {
  if (!SDL_Vulkan_CreateSurface(window_, vulkan_instance_, nullptr,
                                &surface_)) {
    log::error("SDL_VulkanCreateSurface failed");
    return false;
  }
  return true;
}

// Picks the first dedicated GPU if one exists
// Otherwise falls back to integrated or first GPU found.
// todo: score devices if multiple discrete options are found
VkPhysicalDevice Application::findPhysicalDevice() {
  uint32_t phys_device_count = 0;
  vkEnumeratePhysicalDevices(vulkan_instance_, &phys_device_count, nullptr);
  std::vector<VkPhysicalDevice> devices(phys_device_count);
  vkEnumeratePhysicalDevices(vulkan_instance_, &phys_device_count,
                             devices.data());

  VkPhysicalDevice physical_device = nullptr;
  if (phys_device_count) {
    // Pick the first GPU as fallback
    physical_device = devices[0];
    for (auto &p_dev : devices) {
      VkPhysicalDeviceProperties props{};
      vkGetPhysicalDeviceProperties(p_dev, &props);
      if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
        physical_device = p_dev;
        break;
      }
    }
  } else {
    log::error("No physical devices found. Returning nullptr");
  }

  // Ensure that picked device supports swapchain_format_
  uint32_t fmt_count = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface_, &fmt_count, nullptr);
  std::vector<VkSurfaceFormatKHR> surface_formats(fmt_count);
  vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface_, &fmt_count, surface_formats.data());

  bool format_supported = false;
  for (const VkSurfaceFormatKHR &surface_format : surface_formats) {
    if (surface_format.format == swapchain_format_) {
      format_supported = true;
      break;
    }
  }
  if (!format_supported) {
    log::error("Requested swapchain format is not supported by the surface");
    return nullptr;
  }

  return physical_device;
}

// Finds a graphics queue with presentation support.
bool Application::findGraphicsQueue() {
  uint32_t queue_fam_count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties2(physical_device_, &queue_fam_count,
                                            nullptr);
  std::vector<VkQueueFamilyProperties2> queue_fam_props(
      queue_fam_count, {.sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2});
  vkGetPhysicalDeviceQueueFamilyProperties2(physical_device_, &queue_fam_count,
                                            queue_fam_props.data());

  for (size_t current_fam_idx = 0; current_fam_idx < queue_fam_props.size();
       current_fam_idx++) {
    // Ensure queue
    VkBool32 has_present_support = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(physical_device_, current_fam_idx,
                                         surface_, &has_present_support);

    const auto &props = queue_fam_props[current_fam_idx];
    if (props.queueFamilyProperties.queueFlags & VK_QUEUE_GRAPHICS_BIT &&
        has_present_support) {
      gfx_queue_fam_idx_ = current_fam_idx;
      return true;
    }
  }
  return false;
}

bool Application::createDevice(VkPhysicalDevice physical_device) {
  // Query supported features
  VkPhysicalDeviceVulkan14Features supported_features_14{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES,
      .pNext = nullptr};
  VkPhysicalDeviceVulkan13Features supported_features_13{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
      .pNext = &supported_features_14};
  VkPhysicalDeviceVulkan12Features supported_features_12{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
      .pNext = &supported_features_13};
  VkPhysicalDeviceFeatures2 supported_features{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
      .pNext = &supported_features_12};
  vkGetPhysicalDeviceFeatures2(physical_device_, &supported_features);

  if (!supported_features_13.dynamicRendering ||
      !supported_features_13.synchronization2 ||
      !supported_features_12.timelineSemaphore) {
    log::error("Physical device doesn't meet the feature requirements");
    return false;
  }

  VkPhysicalDeviceVulkan14Features features_14{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES,
      .pNext = nullptr};
  VkPhysicalDeviceVulkan13Features features_13{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
      .pNext = &supported_features_14,
      .synchronization2 = VK_TRUE,
      .dynamicRendering = VK_TRUE};
  VkPhysicalDeviceVulkan12Features features_12{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
      .pNext = &supported_features_13,
      .timelineSemaphore = VK_TRUE};
  VkPhysicalDeviceFeatures2 features{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
      .pNext = &supported_features_12};

  // request queues
  std::vector<float> queue_priorities{1.0f};
  VkDeviceQueueCreateInfo gfx_queue_info{
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueFamilyIndex = gfx_queue_fam_idx_,
      .queueCount = 1,
      .pQueuePriorities = queue_priorities.data()};

  // device specific extensions
  const std::vector<const char *> device_extensions{
      VK_KHR_SWAPCHAIN_EXTENSION_NAME};

  VkDeviceCreateInfo dev_create_info{
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .pNext = &features,
      .queueCreateInfoCount = 1,
      .pQueueCreateInfos = &gfx_queue_info,
      .enabledExtensionCount = static_cast<uint32_t>(device_extensions.size()),
      .ppEnabledExtensionNames = device_extensions.data(),
      .pEnabledFeatures = nullptr};

  if (vkCreateDevice(physical_device, &dev_create_info, nullptr, &device_) !=
      VK_SUCCESS) {
    return false;
  }

  vkGetDeviceQueue(device_, gfx_queue_fam_idx_, 0, &gfx_queue_);
  if (!gfx_queue_) {
    log::info("Failed to get graphics queue from device");
    return false;
  }
  return true;
}

bool Application::initializeVMA() {
  VmaVulkanFunctions vma_func_info{};
  VmaAllocatorCreateInfo vma_alloc_info{
    .flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
    .physicalDevice = physical_device_,
    .device = device_,
    .pVulkanFunctions = &vma_func_info,
    .instance = vulkan_instance_,
    .vulkanApiVersion = kVulkanVersion
  };

  vmaImportVulkanFunctionsFromVolk(&vma_alloc_info, &vma_func_info);

  if (vmaCreateAllocator(&vma_alloc_info, &vma_allocator_) != VK_SUCCESS) {
    return false;
  }
  return true;
}

bool Application::createSwapchain(int width, int height) {
  swapchain_width_ = width;
  swapchain_height_ = height;

  VkSurfaceCapabilitiesKHR surface_caps{};
  if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device_, surface_, &surface_caps) != VK_SUCCESS) {
    log::info("Couldn't get the surface capabilities from physical device");
    return false;
  }

  uint32_t requested_image_count = std::max(2u, surface_caps.minImageCount);
  if (surface_caps.maxImageCount > 0) {
    requested_image_count = std::min(requested_image_count, surface_caps.maxImageCount);
  }

  VkSwapchainCreateInfoKHR swapchain_create_info {
    .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
    .surface = surface_,
    .minImageCount = requested_image_count,
    .imageFormat = swapchain_format_,
    .imageColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR,
    .imageExtent{.width = static_cast<uint32_t>(swapchain_width_), .height = static_cast<uint32_t>(swapchain_height_)},
    .imageArrayLayers = 1,
    .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
    .preTransform = surface_caps.currentTransform,
    .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
    .presentMode = VK_PRESENT_MODE_FIFO_KHR};

  if (vkCreateSwapchainKHR(device_, &swapchain_create_info, nullptr, &swapchain_) != VK_SUCCESS) {
    return false;
  }

  // Get swapchain images
  uint32_t image_count = 0;
  vkGetSwapchainImagesKHR(device_, swapchain_, &image_count, nullptr);
  swapchain_images_.resize(image_count);
  vkGetSwapchainImagesKHR(device_, swapchain_, &image_count, swapchain_images_.data());
  swapchain_image_views_.resize(image_count);

  for (size_t i = 0; i < swapchain_images_.size(); ++i) {
    VkImageViewCreateInfo img_view_info {
      .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
      .image = swapchain_images_[i],
      .viewType = VK_IMAGE_VIEW_TYPE_2D,
      .format = swapchain_format_,
      .subresourceRange {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = 1
      }
    };

    if (vkCreateImageView(device_, &img_view_info, nullptr, &swapchain_image_views_[i]) != VK_SUCCESS) {
      log::error("Error creating swapchain image view");
      return false;
    }
  }

  // Semaphores used to signal render completion
  render_complete_semaphores_.resize(swapchain_images_.size());
  for (VkSemaphore &semaphore : render_complete_semaphores_) {
    VkSemaphoreCreateInfo semaphore_info{ .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    if (vkCreateSemaphore(device_, &semaphore_info, nullptr, &semaphore) != VK_SUCCESS) {
      log::error("Error creating the render-complete semaphore");
      return false;
    }
  }

  VkImageCreateInfo depth_create_info {
    .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
    .imageType = VK_IMAGE_TYPE_2D,
    .format = depth_format_,
    .extent{.width = static_cast<uint32_t>(swapchain_width_), .height = static_cast<uint32_t>(swapchain_height_), .depth = 1 },
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
  if (vmaCreateImage(vma_allocator_, &depth_create_info, &alloc_info, &depth_image_, &depth_image_allocation, nullptr) != VK_SUCCESS) {
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
  if (vkCreateImageView(device_, &depth_img_view_info, nullptr, &depth_image_view) != VK_SUCCESS) {
    log::error("Error creating depth image view");
    return false;
  }

  return true;
}

void Application::destroySwapchain() {
  for (VkImageView swapchain_img_view : swapchain_image_views_) {
    vkDestroyImageView(device_, swapchain_img_view, nullptr);
  }
  swapchain_image_views_.clear();

  for (VkSemaphore &semaphore : render_complete_semaphores_) {
    vkDestroySemaphore(device_, semaphore, nullptr);
  }
  render_complete_semaphores_.clear();

  if (swapchain_) {
    vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    swapchain_ = nullptr;
  }

  if (depth_image_view) {
    vkDestroyImageView(device_, depth_image_view, nullptr);
    vmaDestroyImage(vma_allocator_, depth_image_, depth_image_allocation);
    depth_image_view = nullptr;
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
  const std::string shaderPath = "shaders/" + file_name;
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
  if (vkCreateShaderModule(device_, &module_create_info, nullptr, &shader_module) != VK_SUCCESS) {
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
  if (vkCreatePipelineLayout(device_, &pipeline_layout_info, nullptr, &pipeline_layout_) != VK_SUCCESS) {
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

  VkPipelineRenderingCreateInfo render_info {
    .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
    .colorAttachmentCount = 1,
    .pColorAttachmentFormats = &swapchain_format_,
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
  if (vkCreateGraphicsPipelines(device_, nullptr, 1, &pipeline_info, nullptr, &new_pipeline) != VK_SUCCESS) {
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
  if (vkCreateSemaphore(device_, &semaphore_info, nullptr, &timeline_semaphore) != VK_SUCCESS) {
    log::error("Failed to create timeline semaphore");
    return false;
  }

  for (FrameResources &res : frame_resources_) {
    VkSemaphoreCreateInfo semaphore_info{ .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    if (vkCreateSemaphore(device_, &semaphore_info, nullptr, &res.image_acquired_semaphore) != VK_SUCCESS) {
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
      .queueFamilyIndex = gfx_queue_fam_idx_
    };
    if (vkCreateCommandPool(device_, &pool_info, nullptr, &res.command_pool) != VK_SUCCESS) {
      log::error("Unable to create command buffer pool");
      return false;
    }

    VkCommandBufferAllocateInfo cmd_alloc_info {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = res.command_pool,
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1,
    };

    if (vkAllocateCommandBuffers(device_, &cmd_alloc_info, &res.command_buffer) != VK_SUCCESS) {
      log::error("Unable to allocate command buffer");
      return false;
    }
  }
  return true;
}

void Application::run() {
  log::info("hello!");
  log::error("evil hello!");
  running_ = true;
  while (running_) {
    SDL_Event event{0};
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_EVENT_QUIT) {
        running_ = false;
        break;
      } else if (event.type == SDL_EVENT_WINDOW_RESIZED) {
        width_ = event.window.data1;
        height_ = event.window.data2;
        break;
      }
    }
    render();
  }
}

void Application::render() {}

void Application::shutdown() {
  destroySwapchain();
  if (vma_allocator_) {
    vmaDestroyAllocator(vma_allocator_);
  }
  if (surface_) {
    vkDestroySurfaceKHR(vulkan_instance_, surface_, nullptr);
  }
  if (device_) {
    vkDestroyDevice(device_, nullptr);
  }
  if (vulkan_instance_) {
    vkDestroyInstance(vulkan_instance_, nullptr);
  }
  volkFinalize();

  if (window_) {
    SDL_DestroyWindow(window_);
  }
  SDL_Quit();
}

} // namespace strafe
