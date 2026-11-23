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
struct ISession;
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
  bool createSession();

  slang::IGlobalSession *global_session_ = nullptr;
  // One session for every compile, so modules a shader imports (the strafe
  // library) are parsed and checked once instead of once per shader. A
  // session never reloads a module it already has, so hot reload will need a
  // fresh one.
  slang::ISession *session_ = nullptr;
  std::vector<std::string> search_paths_;
};

} // namespace strafe::gfx
