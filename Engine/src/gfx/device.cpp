#include "strafe/gfx/device.h"

#include "strafe/core/flags.h"
#include "strafe/core/log.h"
#include "strafe/gfx/vk_check.h"

#include <SDL3/SDL_vulkan.h>
#include <vector>

namespace strafe::gfx {

namespace {
// Connect Vulkan Debug Callback so validation layer
// messages go through strafe::log and it's related severities.
VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT message_severity, VkDebugUtilsMessageTypeFlagsEXT message_typ,
    const VkDebugUtilsMessengerCallbackDataEXT *p_callback_data, void * /*p_user_data*/) {
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

// Helpers to find layers and extensions.
bool hasInstanceLayer(const char *name) {
  uint32_t count = 0;
  if (!VK_CHECK(vkEnumerateInstanceLayerProperties(&count, nullptr))) {
    return false;
  }
  std::vector<VkLayerProperties> layers(count);
  if (!VK_CHECK(vkEnumerateInstanceLayerProperties(&count, layers.data()))) {
    return false;
  }

  for (VkLayerProperties &layer : layers) {
    if (strcmp(layer.layerName, name) == 0) {
      return true;
    }
  }

  return false;
}

bool hasInstanceExtension(const char *name) {
  uint32_t count = 0;
  if (!VK_CHECK(vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr))) {
    return false;
  }
  std::vector<VkExtensionProperties> extensions(count);
  if (!VK_CHECK(vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data()))) {
    return false;
  }

  for (VkExtensionProperties &extension : extensions) {
    if (strcmp(extension.extensionName, name) == 0) {
      return true;
    }
  }

  return false;
}
} // namespace

bool Device::initialize(SDL_Window *window, VkFormat requested_swapchain_format) {
  if (!createVulkanInstance()) {
    log::error("Failed to create vulkan instance");
    return false;
  }
  if (!createSurface(window)) {
    log::error("Failed to create surface");
    return false;
  }
  if (!findPhysicalDevice(requested_swapchain_format)) {
    log::error("Failed to find fitting physical device");
    return false;
  }
  if (!findGraphicsQueue()) {
    log::error("Failed to find graphics queue on physical device");
    return false;
  }
  if (!createLogicalDevice()) {
    log::error("Failed to create device");
    return false;
  }
  if (!initializeVMA()) {
    log::error("Failed to initialize VMA");
    return false;
  }
  return true;
}

void Device::shutdown() {
  if (instance_) {
    if (logical_device_) {
      if (vma_allocator_) {
        vmaDestroyAllocator(vma_allocator_);
      }
      if (surface_) {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
      }
      vkDestroyDevice(logical_device_, nullptr);
    }
    if (debug_messenger_) {
      vkDestroyDebugUtilsMessengerEXT(instance_, debug_messenger_, nullptr);
    }
    vkDestroyInstance(instance_, nullptr);
  }
  volkFinalize();
}

bool Device::initializeVMA() {
  if (!instance_) {
    log::error("VMA cannot be initialized without a VkInstance");
    return false;
  }
  if (!logical_device_) {
    log::error("VMA cannot be initialized without a VkDevice");
    return false;
  }

  VmaVulkanFunctions vma_func_info{};
  VmaAllocatorCreateInfo vma_alloc_info{
      .flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
      .physicalDevice = physical_device_,
      .device = logical_device_,
      .pVulkanFunctions = &vma_func_info,
      .instance = instance_,
      .vulkanApiVersion = kVulkanVersion};

  vmaImportVulkanFunctionsFromVolk(&vma_alloc_info, &vma_func_info);

  if (!VK_CHECK(vmaCreateAllocator(&vma_alloc_info, &vma_allocator_))) {
    return false;
  }
  return true;
}

bool Device::createVulkanInstance() {
  if (!VK_CHECK(volkInitialize())) {
    return false;
  }

  VkApplicationInfo app_info{
      .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
      .pApplicationName = "Strafe Engine Application",
      .apiVersion = kVulkanVersion,
  };

  // Find required extensions for platform
  uint32_t inst_ext_count = 0;
  const char *const *extensions = SDL_Vulkan_GetInstanceExtensions(&inst_ext_count);

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
      .enabledExtensionCount = static_cast<uint32_t>(requested_extensions.size()),
      .ppEnabledExtensionNames = requested_extensions.data(),
  };

  VkDebugUtilsMessengerCreateInfoEXT debug_info{};
  if (use_debug_utils) {
    debug_info = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = debugCallback};
    inst_create_info.pNext = &debug_info;
  }

  if (!VK_CHECK(vkCreateInstance(&inst_create_info, nullptr, &instance_))) {
    return false;
  }

  volkLoadInstance(instance_);

  if (use_debug_utils &&
      !VK_CHECK(vkCreateDebugUtilsMessengerEXT(instance_, &debug_info, nullptr, &debug_messenger_))) {
    return false;
  }

  return true;
}

bool Device::createSurface(SDL_Window *window) {
  if (!instance_) {
    log::error("Cannot create surface without valid VkInstance");
    return false;
  }
  if (!SDL_Vulkan_CreateSurface(window, instance_, nullptr, &surface_)) {
    log::error("SDL_Vulkan_CreateSurface failed");
    return false;
  }
  return true;
}

bool Device::findPhysicalDevice(VkFormat requested_swapchain_format) {
  uint32_t phys_device_count = 0;
  if (!VK_CHECK(vkEnumeratePhysicalDevices(instance_, &phys_device_count, nullptr))) {
    return false;
  }
  std::vector<VkPhysicalDevice> devices(phys_device_count);
  if (!VK_CHECK(vkEnumeratePhysicalDevices(instance_, &phys_device_count, devices.data()))) {
    return false;
  }

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
    log::error("No physical devices found");
    return false;
  }

  // Ensure that picked device supports swapchain_format_
  uint32_t fmt_count = 0;
  if (!VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface_, &fmt_count, nullptr))) {
    return false;
  }
  std::vector<VkSurfaceFormatKHR> surface_formats(fmt_count);
  if (!VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface_, &fmt_count, surface_formats.data()))) {
    return false;
  }

  bool format_supported = false;
  for (const VkSurfaceFormatKHR &surface_format : surface_formats) {
    if (surface_format.format == requested_swapchain_format) {
      format_supported = true;
      break;
    }
  }
  if (!format_supported) {
    log::error("Requested swapchain format is not supported by the surface");
    return false;
  }

  physical_device_ = physical_device;
  return true;
}

bool Device::findGraphicsQueue() {
  uint32_t queue_fam_count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties2(physical_device_, &queue_fam_count, nullptr);
  std::vector<VkQueueFamilyProperties2> queue_fam_props(
      queue_fam_count, {.sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2});
  vkGetPhysicalDeviceQueueFamilyProperties2(physical_device_, &queue_fam_count, queue_fam_props.data());

  for (size_t current_fam_idx = 0; current_fam_idx < queue_fam_props.size(); current_fam_idx++) {
    // Ensure queue
    VkBool32 has_present_support = VK_FALSE;
    if (!VK_CHECK(
            vkGetPhysicalDeviceSurfaceSupportKHR(physical_device_, current_fam_idx, surface_, &has_present_support))) {
      continue;
    }

    const auto &props = queue_fam_props[current_fam_idx];
    if (props.queueFamilyProperties.queueFlags & VK_QUEUE_GRAPHICS_BIT && has_present_support) {
      graphics_queue_family_idx_ = current_fam_idx;
      return true;
    }
  }
  return false;
}

bool Device::createLogicalDevice() {
  // Query supported features
  VkPhysicalDeviceVulkan14Features supported_features_14{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES, .pNext = nullptr};
  VkPhysicalDeviceVulkan13Features supported_features_13{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES, .pNext = &supported_features_14};
  VkPhysicalDeviceVulkan12Features supported_features_12{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, .pNext = &supported_features_13};
  VkPhysicalDeviceFeatures2 supported_features{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &supported_features_12};
  vkGetPhysicalDeviceFeatures2(physical_device_, &supported_features);

  if (!supported_features_14.maintenance5 || !supported_features_13.dynamicRendering ||
      !supported_features_13.synchronization2 || !supported_features_12.timelineSemaphore ||
      !supported_features_12.bufferDeviceAddress || !supported_features_12.scalarBlockLayout ||
      // Bindless descriptors, see BindlessSet.
      !supported_features_12.runtimeDescriptorArray || !supported_features_12.descriptorBindingPartiallyBound ||
      !supported_features_12.descriptorBindingSampledImageUpdateAfterBind ||
      !supported_features_12.descriptorBindingStorageImageUpdateAfterBind ||
      !supported_features_12.shaderSampledImageArrayNonUniformIndexing ||
      !supported_features_12.shaderStorageImageArrayNonUniformIndexing ||
      !supported_features.features.samplerAnisotropy) {
    log::error("Physical device doesn't meet the feature requirements");
    return false;
  }

  VkPhysicalDeviceVulkan14Features features_14{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES, .pNext = nullptr, .maintenance5 = VK_TRUE};
  VkPhysicalDeviceVulkan13Features features_13{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
      .pNext = &features_14,
      .synchronization2 = VK_TRUE,
      .dynamicRendering = VK_TRUE};
  VkPhysicalDeviceVulkan12Features features_12{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
      .pNext = &features_13,
      .shaderSampledImageArrayNonUniformIndexing = VK_TRUE,
      .shaderStorageImageArrayNonUniformIndexing = VK_TRUE,
      .descriptorBindingSampledImageUpdateAfterBind = VK_TRUE,
      .descriptorBindingStorageImageUpdateAfterBind = VK_TRUE,
      .descriptorBindingPartiallyBound = VK_TRUE,
      .runtimeDescriptorArray = VK_TRUE,
      .scalarBlockLayout = VK_TRUE,
      .timelineSemaphore = VK_TRUE,
      .bufferDeviceAddress = VK_TRUE};
  VkPhysicalDeviceFeatures2 features{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
      .pNext = &features_12,
      .features{.samplerAnisotropy = VK_TRUE}};

  // request queues
  std::vector<float> queue_priorities{1.0f};
  VkDeviceQueueCreateInfo gfx_queue_info{
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      .queueFamilyIndex = graphics_queue_family_idx_,
      .queueCount = 1,
      .pQueuePriorities = queue_priorities.data()};

  // device specific extensions
  const std::vector<const char *> device_extensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};

  VkDeviceCreateInfo dev_create_info{
      .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      .pNext = &features,
      .queueCreateInfoCount = 1,
      .pQueueCreateInfos = &gfx_queue_info,
      .enabledExtensionCount = static_cast<uint32_t>(device_extensions.size()),
      .ppEnabledExtensionNames = device_extensions.data(),
      .pEnabledFeatures = nullptr};

  if (!VK_CHECK(vkCreateDevice(physical_device_, &dev_create_info, nullptr, &logical_device_))) {
    return false;
  }

  vkGetDeviceQueue(logical_device_, graphics_queue_family_idx_, 0, &graphics_queue_);
  if (!graphics_queue_) {
    log::error("Failed to get graphics queue from device");
    return false;
  }
  return true;
}

} // namespace strafe::gfx
