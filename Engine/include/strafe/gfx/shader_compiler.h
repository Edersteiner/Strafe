#pragma once

#include "strafe/gfx/shader_reflection.h"

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
  ShaderReflection reflection;
};

// Compiles Slang shaders to SPIR-V. Shaders are found by module name in the
// search paths, in order, so "lit" is the first <search path>/lit.slang and
// `import strafe;` finds strafe.slang the same way.
class ShaderCompiler {
public:
  ShaderCompiler() = default;
  ~ShaderCompiler();

  ShaderCompiler(const ShaderCompiler &) = delete;
  ShaderCompiler &operator=(const ShaderCompiler &) = delete;

  bool initialize(std::vector<std::string> search_paths);
  void shutdown();

  // Compiles every entry point in the module and fills in its reflection.
  // Fails on unexpected attributes.
  std::optional<CompiledShader> compile(std::string_view module_name);

private:
  slang::IGlobalSession *global_session_ = nullptr;
  std::vector<std::string> search_paths_;
};

} // namespace strafe::gfx
