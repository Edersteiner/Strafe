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

namespace {
bool hasInstanceLayer(const char* name) {
  uint32_t count = 0;
  vkEnumerateInstanceLayerProperties(&count, nullptr);
  std::vector<VkLayerProperties> layers(count);
  vkEnumerateInstanceLayerProperties(&count, layers.data());

  for (VkLayerProperties &layer : layers) {
    if (strcmp(layer.layerName, name) == 0) { return true; }
  }

  return false;
}

bool hasInstanceExtension(const char* name) {
  uint32_t count = 0;
  vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
  std::vector<VkExtensionProperties> extensions(count);
  vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data());

  for (VkExtensionProperties &extension : extensions) {
    if (strcmp(extension.extensionName, name) == 0) { return true; }
  }

  return false;
}
}

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
  if (!createSwapchain()) {
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

  bool use_validation_layer = flags::kEnableValidation && hasInstanceLayer("VK_LAYER_KHRONOS_validation");
  bool use_debug_utils = flags::kEnableValidation && hasInstanceExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

  std::vector<const char *> requested_extensions{};

  if (use_debug_utils) {
    requested_extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  }

  for (size_t i = 0; i < inst_ext_count; ++i) {
    requested_extensions.push_back(extensions[i]);
  }

  std::vector<const char *> requested_layers{};

  if (use_validation_layer) {
    requested_layers.push_back("VK_LAYER_KHRONOS_validation");
  }

  VkInstanceCreateInfo inst_create_info{
    .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
    .pApplicationInfo = &app_info,
    .enabledLayerCount = static_cast<uint32_t>(requested_layers.size()),
    .ppEnabledLayerNames = requested_layers.data(),
    .enabledExtensionCount =
      static_cast<uint32_t>(requested_extensions.size()),
    .ppEnabledExtensionNames = requested_extensions.data(),
  };

  VkDebugUtilsMessengerCreateInfoEXT debug_info{};
  if (use_debug_utils) {
    debug_info = {
      .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
      .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
      .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
      .pfnUserCallback = debugCallback};
    inst_create_info.pNext = &debug_info;
  }

  if (vkCreateInstance(&inst_create_info, nullptr, &vulkan_instance_) !=
      VK_SUCCESS) {
    return false;
  }

  volkLoadInstance(vulkan_instance_);

  if (use_debug_utils) {
    if (vkCreateDebugUtilsMessengerEXT(vulkan_instance_, &debug_info, nullptr, &debug_messenger_) != VK_SUCCESS) {
      log::error("Failed to create debug messenger");
      return false;
    }
  }

  return true;
}

bool Application::createSurface() {
  if (!SDL_Vulkan_CreateSurface(window_, vulkan_instance_, nullptr,
                                &surface_)) {
    log::error("SDL_Vulkan_CreateSurface failed");
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

  if (!supported_features_14.maintenance5 ||
      !supported_features_13.dynamicRendering ||
      !supported_features_13.synchronization2 ||
      !supported_features_12.timelineSemaphore ||
      !supported_features_12.bufferDeviceAddress ||
      !supported_features_12.scalarBlockLayout) {
    log::error("Physical device doesn't meet the feature requirements");
    return false;
  }

  VkPhysicalDeviceVulkan14Features features_14{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES,
      .pNext = nullptr,
      .maintenance5 = VK_TRUE};
  VkPhysicalDeviceVulkan13Features features_13{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
      .pNext = &features_14,
      .synchronization2 = VK_TRUE,
      .dynamicRendering = VK_TRUE};
  VkPhysicalDeviceVulkan12Features features_12{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
      .pNext = &features_13,
      .scalarBlockLayout = VK_TRUE,
      .timelineSemaphore = VK_TRUE,
      .bufferDeviceAddress = VK_TRUE};
  VkPhysicalDeviceFeatures2 features{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
      .pNext = &features_12};

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

VkExtent2D Application::chooseSwapchainExtent(const VkSurfaceCapabilitiesKHR &caps) const {
  // If the current extent has a decided size already, use it.
  if (caps.currentExtent.width != UINT32_MAX) {
    return caps.currentExtent;
  }

  // If it doesn't, set extent based on our window.
  VkExtent2D extent {
    .width = static_cast<uint32_t>(width_),
    .height = static_cast<uint32_t>(height_)
  };

  // Clamp extent to what the driver allows
  extent.width = std::clamp(extent.width, caps.minImageExtent.width, caps.maxImageExtent.width);
  extent.height = std::clamp(extent.height, caps.minImageExtent.height, caps.maxImageExtent.height);

  return extent;
}

bool Application::createSwapchain() {
  VkSurfaceCapabilitiesKHR surface_caps{};
  if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device_, surface_, &surface_caps) != VK_SUCCESS) {
    log::info("Couldn't get the surface capabilities from physical device");
    return false;
  }

  VkExtent2D extent = chooseSwapchainExtent(surface_caps);

  // If extent width or height is 0, the window is invisible or minimized.
  // Do not return a swapchain in that case
  if (extent.width == 0 || extent.height == 0) {
    swapchain_ = VK_NULL_HANDLE;
    return true;
  }

  swapchain_width_ = extent.width;
  swapchain_height_ = extent.height;

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
    vkDeviceWaitIdle(device_);
    destroySwapchain();
    if (!createSwapchain()) {
      log::error("Cannot recreate swapchain!");
      running_ = false;
      return;
    }
    require_swapchain_recreate = (swapchain_ == VK_NULL_HANDLE);
  }

  // No swapchain (window minimized, etc.) skip rendering.
  if (swapchain_ == VK_NULL_HANDLE) {
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
  vkWaitSemaphores(device_, &wait_info, UINT64_MAX);

  FrameResources &res = frame_resources_[frame_res_index];
  vkResetCommandPool(device_, res.command_pool, 0);

  VkSemaphore image_acquire_semaphore = frame_resources_[frame_res_index].image_acquired_semaphore;

  uint32_t image_index = 0;
  VkResult acquire_result = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, image_acquire_semaphore, VK_NULL_HANDLE, &image_index);

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
      .image = swapchain_images_[image_index],
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
    .imageView = swapchain_image_views_[image_index],
    .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR, // Clear the image
    .storeOp = VK_ATTACHMENT_STORE_OP_STORE, // keep dAta for presentation
    .clearValue{.color{0.01f, 0.01f, 0.01f, 1}}
  };
  VkRenderingAttachmentInfo depth_attachment_info {
    .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
    .imageView = depth_image_view,
    .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
    .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
    .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
    .clearValue{.depthStencil{1.0f, 0}}
  };

  VkRenderingInfo rendering_info {
    .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
    .renderArea {
      .offset{.x = 0, .y = 0},
      .extent{.width = static_cast<uint32_t>(swapchain_width_), .height = static_cast<uint32_t>(swapchain_height_)}
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
      .width = static_cast<float>(swapchain_width_),
      .height = static_cast<float>(swapchain_height_),
      .minDepth = 0,
      .maxDepth = 1,
    };
    vkCmdSetViewport(res.command_buffer, 0, 1, &viewport);

    VkRect2D scissor {
      .offset{.x = 0, .y = 0},
      .extent{.width = static_cast<uint32_t>(swapchain_width_), .height = static_cast<uint32_t>(swapchain_height_)}
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
    .image = swapchain_images_[image_index],
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
      .semaphore = render_complete_semaphores_[image_index],
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
  vkQueueSubmit2(gfx_queue_, 1, &submit_info, VK_NULL_HANDLE);

  VkPresentInfoKHR present_info {
    .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
    .waitSemaphoreCount = 1,
    .pWaitSemaphores = &render_complete_semaphores_[image_index],
    .swapchainCount = 1,
    .pSwapchains = &swapchain_,
    .pImageIndices = &image_index,
    .pResults = nullptr
  };

  vkQueuePresentKHR(gfx_queue_, &present_info);
}

void Application::shutdown() {
  vkDeviceWaitIdle(device_);

  if (timeline_semaphore) {
    vkDestroySemaphore(device_, timeline_semaphore, nullptr);
  }
  for (auto &res : frame_resources_) {
    vkDestroySemaphore(device_, res.image_acquired_semaphore, nullptr);
    vkDestroyCommandPool(device_, res.command_pool, nullptr); // Implicitly destroys buffers too
  }
  if (pipeline_layout_) {
    vkDestroyPipelineLayout(device_, pipeline_layout_, nullptr);
  }
  if (pipeline_) {
    vkDestroyPipeline(device_, pipeline_, nullptr);
  }

  if (vert_shader_) {
    vkDestroyShaderModule(device_, vert_shader_, nullptr);
  }
  if (frag_shader_) {
    vkDestroyShaderModule(device_, frag_shader_, nullptr);
  }

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
  if (debug_messenger_) {
    vkDestroyDebugUtilsMessengerEXT(vulkan_instance_, debug_messenger_, nullptr);
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
