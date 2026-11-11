#include "gfx/pipeline_manager.h"

#include "core/log.h"
#include "gfx/command_list.h"
#include "gfx/device.h"
#include "gfx/framering.h"
#include "gfx/vk_check.h"

#include <array>
#include <cassert>

namespace strafe::gfx {

namespace {

VkPipelineColorBlendAttachmentState blendState(BlendMode mode) {
  VkPipelineColorBlendAttachmentState state{
      .blendEnable = VK_FALSE,
      .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT};
  switch (mode) {
  case BlendMode::Opaque:
    break;
  case BlendMode::Alpha:
    state.blendEnable = VK_TRUE;
    state.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    state.colorBlendOp = VK_BLEND_OP_ADD;
    state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    state.alphaBlendOp = VK_BLEND_OP_ADD;
    break;
  case BlendMode::Additive:
    state.blendEnable = VK_TRUE;
    state.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
    state.colorBlendOp = VK_BLEND_OP_ADD;
    state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    state.alphaBlendOp = VK_BLEND_OP_ADD;
    break;
  }
  return state;
}

bool hasStage(const CompiledShader &shader, VkShaderStageFlagBits stage) {
  for (const CompiledStage &compiled : shader.stages) {
    if (compiled.stage == stage) {
      return true;
    }
  }
  return false;
}

} // namespace

bool PipelineManager::initialize(Device &device, FrameRing &frame_ring,
                                 VkDescriptorSetLayout bindless_layout,
                                 SharedStructSizes shared_sizes,
                                 std::string shader_directory) {
  device_ = &device;
  frame_ring_ = &frame_ring;
  shared_sizes_ = shared_sizes;
  if (shared_sizes_.push_constants > kPushConstantRangeSize) {
    log::error("Push constants of {} bytes exceed the {} byte range",
               shared_sizes_.push_constants, kPushConstantRangeSize);
    return false;
  }

  if (!compiler_.initialize(std::move(shader_directory))) {
    return false;
  }

  const VkPushConstantRange push_range{.stageFlags = kPushConstantStages,
                                       .offset = 0,
                                       .size = kPushConstantRangeSize};
  const VkPipelineLayoutCreateInfo layout_info{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 1,
      .pSetLayouts = &bindless_layout,
      .pushConstantRangeCount = 1,
      .pPushConstantRanges = &push_range};
  return VK_CHECK(vkCreatePipelineLayout(device_->logicalDevice(), &layout_info,
                                         nullptr, &layout_));
}

void PipelineManager::shutdown() {
  if (!device_) {
    return;
  }
  const VkDevice device = device_->logicalDevice();
  for (Shader &shader : shaders_) {
    if (shader.pipeline) {
      frame_ring_->deferDestroy([device, pipeline = shader.pipeline] {
        vkDestroyPipeline(device, pipeline, nullptr);
      });
    }
  }
  shaders_.clear();
  if (layout_) {
    frame_ring_->deferDestroy([device, layout = layout_] {
      vkDestroyPipelineLayout(device, layout, nullptr);
    });
    layout_ = VK_NULL_HANDLE;
  }
  passes_.clear();
  compiler_.shutdown();
  device_ = nullptr;
}

void PipelineManager::registerPass(const std::string &name, PassDesc desc) {
  passes_[name] = std::move(desc);
}

std::optional<ShaderId> PipelineManager::load(std::string_view name) {
  std::optional<CompiledShader> compiled = compiler_.compile(name);
  if (!compiled) {
    return std::nullopt;
  }
  const ShaderReflection &reflection = compiled->reflection;

  const auto pass = passes_.find(reflection.pass);
  if (pass == passes_.end()) {
    log::error("Shader {}: [Pass(\"{}\")] is not a registered pass", name,
               reflection.pass);
    return std::nullopt;
  }
  if (!validate(*compiled, pass->second)) {
    return std::nullopt;
  }

  const VkPipeline pipeline = createPipeline(*compiled, pass->second);
  if (!pipeline) {
    return std::nullopt;
  }

  log::debug("Shader {}: pass {}, blend {}, cull {}, depth {}, {} color "
             "output(s), {} byte push constants, {} byte frame data",
             name, reflection.pass, static_cast<int>(reflection.blend),
             static_cast<int>(reflection.cull),
             static_cast<int>(reflection.depth), reflection.color_outputs,
             reflection.push_constant_size, reflection.frame_data_size);

  shaders_.push_back({.reflection = reflection, .pipeline = pipeline});
  return ShaderId{.index = static_cast<uint32_t>(shaders_.size() - 1)};
}

void PipelineManager::bind(CommandList &cmd, ShaderId id) const {
  assert(id.index < shaders_.size() && "Invalid ShaderId");
  const Shader &shader = shaders_[id.index];
  cmd.bindPipeline(shader.pipeline, layout_);
  cmd.setCullMode(shader.reflection.cull);
  cmd.setDepthMode(shader.reflection.depth);
}

const ShaderReflection &PipelineManager::reflection(ShaderId id) const {
  assert(id.index < shaders_.size() && "Invalid ShaderId");
  return shaders_[id.index].reflection;
}

bool PipelineManager::validate(const CompiledShader &shader,
                               const PassDesc &pass) const {
  const ShaderReflection &reflection = shader.reflection;
  const std::string &name = reflection.name;

  if (!hasStage(shader, VK_SHADER_STAGE_VERTEX_BIT) ||
      !hasStage(shader, VK_SHADER_STAGE_FRAGMENT_BIT)) {
    log::error("Shader {} needs a vertex and a fragment entry point", name);
    return false;
  }
  if (reflection.push_constant_size != 0 &&
      reflection.push_constant_size != shared_sizes_.push_constants) {
    log::error("Shader {}: push constant block is {} bytes, but the engine "
               "pushes {}. DrawPush in strafe/draw.slang and the C++ struct "
               "have drifted apart",
               name, reflection.push_constant_size,
               shared_sizes_.push_constants);
    return false;
  }
  if (reflection.frame_data_size != 0 &&
      reflection.frame_data_size != shared_sizes_.frame_data) {
    log::error("Shader {}: FrameData is {} bytes, but the engine writes {}. "
               "FrameData in strafe/frame.slang and the C++ struct have "
               "drifted apart",
               name, reflection.frame_data_size, shared_sizes_.frame_data);
    return false;
  }
  if (reflection.color_outputs != pass.color_formats.size()) {
    log::error("Shader {} writes {} color output(s), but pass {} has {} color "
               "target(s)",
               name, reflection.color_outputs, reflection.pass,
               pass.color_formats.size());
    return false;
  }
  return true;
}

VkPipeline PipelineManager::createPipeline(const CompiledShader &shader,
                                           const PassDesc &pass) const {
  // maintenance5 lets each stage carry its SPIR-V directly, so there are no
  // VkShaderModule objects to create and destroy.
  std::vector<VkShaderModuleCreateInfo> module_infos;
  module_infos.reserve(shader.stages.size());
  std::vector<VkPipelineShaderStageCreateInfo> stages;
  for (const CompiledStage &stage : shader.stages) {
    module_infos.push_back(
        {.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
         .codeSize = stage.spirv.size() * sizeof(uint32_t),
         .pCode = stage.spirv.data()});
    stages.push_back(
        {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .pNext = &module_infos.back(),
         .stage = stage.stage,
         .module = VK_NULL_HANDLE,
         .pName = stage.entry_point.c_str()});
  }

  // Vertices are pulled through buffer addresses, so there is no vertex input.
  const VkPipelineVertexInputStateCreateInfo vertex_input{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  const VkPipelineInputAssemblyStateCreateInfo input_assembly{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
  const VkPipelineViewportStateCreateInfo viewport{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .viewportCount = 1,
      .scissorCount = 1};
  // cullMode is dynamic, set by bind().
  const VkPipelineRasterizationStateCreateInfo raster{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .polygonMode = VK_POLYGON_MODE_FILL,
      .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
      .lineWidth = 1.0f};
  const VkPipelineMultisampleStateCreateInfo multisample{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
  // Test, write and compare op are dynamic, set by bind().
  const VkPipelineDepthStencilStateCreateInfo depth_stencil{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};

  const std::vector<VkPipelineColorBlendAttachmentState> blend_attachments(
      pass.color_formats.size(), blendState(shader.reflection.blend));
  const VkPipelineColorBlendStateCreateInfo blend{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .attachmentCount = static_cast<uint32_t>(blend_attachments.size()),
      .pAttachments = blend_attachments.data()};

  constexpr std::array dynamic_states{
      VK_DYNAMIC_STATE_VIEWPORT,          VK_DYNAMIC_STATE_SCISSOR,
      VK_DYNAMIC_STATE_CULL_MODE,         VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE,
      VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE, VK_DYNAMIC_STATE_DEPTH_COMPARE_OP};
  const VkPipelineDynamicStateCreateInfo dynamic{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
      .dynamicStateCount = static_cast<uint32_t>(dynamic_states.size()),
      .pDynamicStates = dynamic_states.data()};

  const VkPipelineRenderingCreateInfo rendering{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
      .colorAttachmentCount = static_cast<uint32_t>(pass.color_formats.size()),
      .pColorAttachmentFormats = pass.color_formats.data(),
      .depthAttachmentFormat = pass.depth_format};

  const VkGraphicsPipelineCreateInfo pipeline_info{
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .pNext = &rendering,
      .stageCount = static_cast<uint32_t>(stages.size()),
      .pStages = stages.data(),
      .pVertexInputState = &vertex_input,
      .pInputAssemblyState = &input_assembly,
      .pViewportState = &viewport,
      .pRasterizationState = &raster,
      .pMultisampleState = &multisample,
      .pDepthStencilState = &depth_stencil,
      .pColorBlendState = &blend,
      .pDynamicState = &dynamic,
      .layout = layout_};

  VkPipeline pipeline = VK_NULL_HANDLE;
  if (!VK_CHECK(vkCreateGraphicsPipelines(device_->logicalDevice(),
                                          VK_NULL_HANDLE, 1, &pipeline_info,
                                          nullptr, &pipeline))) {
    return VK_NULL_HANDLE;
  }
  return pipeline;
}

} // namespace strafe::gfx
