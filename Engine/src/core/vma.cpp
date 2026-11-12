// Compiles the VMA implementation. Must be the only translation unit that
// defines VMA_IMPLEMENTATION.
#include <volk.h>

// Function pointers come from volk via vmaImportVulkanFunctionsFromVolk.
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 0
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
