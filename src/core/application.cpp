#include "core/application.h"

#include <SDL3/SDL_vulkan.h>

#include <vector>

#include "core/log.h"

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
    log::error("Failed to create Vulkan instance");
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
