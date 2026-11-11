#pragma once

#include <volk.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace slang {
struct IGlobalSession;
} // namespace slang

namespace strafe::gfx {

struct CompiledStage {
  VkShaderStageFlagBits stage = VK_SHADER_STAGE_VERTEX_BIT;
  std::string entry_point;
  std::vector<uint32_t> spirv;
};

struct CompiledShader {
  std::vector<CompiledStage> stages;
};

// Compiles Slang shaders to SPIR-V. Shaders are found by module name under the
// search path, so "lit" is <search path>/lit.slang and `import strafe;` finds
// strafe.slang next to it.
class ShaderCompiler {
public:
  ShaderCompiler() = default;
  ~ShaderCompiler();

  ShaderCompiler(const ShaderCompiler &) = delete;
  ShaderCompiler &operator=(const ShaderCompiler &) = delete;

  bool initialize(std::string search_path);
  void shutdown();

  // Compiles every entry point in the module.
  std::optional<CompiledShader> compile(std::string_view module_name);

private:
  slang::IGlobalSession *global_session_ = nullptr;
  std::string search_path_;
};

} // namespace strafe::gfx
