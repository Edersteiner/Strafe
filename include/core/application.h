#pragma once

#include "gfx/device.h"
#include "gfx/framering.h"
#include "gfx/swapchain.h"

#include <string>

#include <volk.h>
#include <SDL3/SDL.h>

// Include after volk. The implementation is compiled in src/core/vma.cpp.
#include <vk_mem_alloc.h>

// Todo: replace shaderc and glsl with Slang.
#include <shaderc/shaderc.hpp>

namespace strafe {

class Application {
public:
  bool initialize();
  void run();
  void shutdown();

private:
  bool initializeVulkan();
  bool createDepthBuffer();
  void destroyDepthBuffer();
  bool createShaders();
  VkShaderModule createShaderModule(const std::string &file_name,
                                    shaderc_shader_kind kind) const;
  VkPipeline createGraphicsPipeline();

  void render();

  bool running_ = false;

  SDL_Window *window_ = nullptr;
  int width_ = 1280, height_ = 720;

  gfx::Device device_;
  gfx::Swapchain swapchain_;
  gfx::FrameRing frame_ring_;

  VkImage depth_image_ = VK_NULL_HANDLE;
  VkImageView depth_image_view_ = VK_NULL_HANDLE;
  VmaAllocation depth_image_allocation_ = VK_NULL_HANDLE;
  VkFormat depth_format_ = VK_FORMAT_D32_SFLOAT;

  VkShaderModule vert_shader_ = VK_NULL_HANDLE;
  VkShaderModule frag_shader_ = VK_NULL_HANDLE;

  VkPipeline pipeline_ = VK_NULL_HANDLE;
  VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
};
} // namespace strafe
