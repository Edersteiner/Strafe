#pragma once

#include "strafe/gfx/shader_compiler.h"
#include "strafe/gfx/shader_reflection.h"

#include <volk.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace strafe::gfx {

class CommandList;
class Device;
class FrameRing;

// Refers to a shader loaded by PipelineManager::load
struct ShaderId {
  uint32_t index = UINT32_MAX;

  bool valid() const { return index != UINT32_MAX; }
};

// The target formats of a render pass. Shaders name their pass with
// [Pass("...")], "forward" by default.
struct PassDesc {
  std::vector<VkFormat> color_formats;
  VkFormat depth_format = VK_FORMAT_UNDEFINED;
};

// sizeof the C++ structs every shader shares with the engine.
struct SharedStructSizes {
  // The struct every draw pushes, DrawPush in strafe/draw.slang.
  uint32_t push_constants = 0;
  // What DrawPush::frame points to, FrameData in strafe/frame.slang.
  uint32_t frame_data = 0;
};

// Turns Slang shaders into pipelines. Everything a pipeline needs comes from
// the shader's reflection and attributes.
//
// Every pipeline shares one layout, the bindless set plus one push constant
// range, so any shader can be bound without rebinding descriptors. Cull mode
// and depth state are dynamic and applied by bind(), only blend state and
// the pass formats are specified in the pipeline.
class PipelineManager {
public:
  static constexpr uint32_t kPushConstantRangeSize = 256;

  PipelineManager() = default;
  ~PipelineManager() = default;

  PipelineManager(const PipelineManager &) = delete;
  PipelineManager &operator=(const PipelineManager &) = delete;

  bool initialize(
      Device &device, FrameRing &frame_ring, VkDescriptorSetLayout bindless_layout, SharedStructSizes shared_sizes,
      std::vector<std::string> shader_directories);
  // Destruction goes through the frame ring's deletion queue.
  void shutdown();

  // Must happen before loading shaders that target the pass.
  void registerPass(const std::string &name, PassDesc desc);

  // Compiles <name>.slang from the first shader directory that has it, and
  // builds its pipeline.
  std::optional<ShaderId> load(std::string_view name);

  // Binds the pipeline and applies its dynamic state.
  void bind(CommandList &cmd, ShaderId id) const;

  const ShaderReflection &reflection(ShaderId id) const;
  VkPipelineLayout layout() const { return layout_; }

private:
  struct Shader {
    ShaderReflection reflection;
    VkPipeline pipeline = VK_NULL_HANDLE;
  };

  bool validate(const CompiledShader &shader, const PassDesc &pass) const;
  VkPipeline createPipeline(const CompiledShader &shader, const PassDesc &pass) const;

  Device *device_ = nullptr;
  FrameRing *frame_ring_ = nullptr;
  ShaderCompiler compiler_;
  VkPipelineLayout layout_ = VK_NULL_HANDLE;
  SharedStructSizes shared_sizes_;

  std::unordered_map<std::string, PassDesc> passes_;
  std::vector<Shader> shaders_;
};

} // namespace strafe::gfx
