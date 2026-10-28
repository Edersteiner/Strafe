#include "gfx/device.h"

#include "core/log.h"
#include "core/flags.h""

#include <vector>
#include <SDL3/SDL_vulkan.h>

namespace strafe::gfx {

// Connect Vulkan Debug Callback so validation layer
// messages go through strafe::log
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

bool Device::initialize(SDL_Window* window) {
  return true;
}

bool Device::createVulkanInstance() {
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

  if (vkCreateInstance(&inst_create_info, nullptr, &instance_) !=
      VK_SUCCESS) {
    return false;
  }

  volkLoadInstance(instance_);

  if (use_debug_utils) {
    if (vkCreateDebugUtilsMessengerEXT(instance_, &debug_info, nullptr, &debug_messenger_) != VK_SUCCESS) {
      log::error("Failed to create debug messenger");
      return false;
    }
  }

  return true;
}

}
