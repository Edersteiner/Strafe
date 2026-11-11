#include "gfx/shader_compiler.h"

#include "core/log.h"

#include <slang-com-ptr.h>
#include <slang.h>

#include <array>
#include <cctype>
#include <cstring>
#include <unordered_map>

namespace strafe::gfx {

namespace {

// Logs Slang's diagnostics, which can hold warnings even on success.
void logDiagnostics(slang::IBlob *diagnostics, bool failed) {
  if (!diagnostics || diagnostics->getBufferSize() == 0) {
    return;
  }
  const std::string_view text(
      static_cast<const char *>(diagnostics->getBufferPointer()),
      diagnostics->getBufferSize());
  if (failed) {
    log::error("Slang: {}", text);
  } else {
    log::warn("Slang: {}", text);
  }
}

std::optional<VkShaderStageFlagBits> toVkStage(SlangStage stage) {
  switch (stage) {
  case SLANG_STAGE_VERTEX:
    return VK_SHADER_STAGE_VERTEX_BIT;
  case SLANG_STAGE_FRAGMENT:
    return VK_SHADER_STAGE_FRAGMENT_BIT;
  case SLANG_STAGE_COMPUTE:
    return VK_SHADER_STAGE_COMPUTE_BIT;
  default:
    return std::nullopt;
  }
}

// For now attributes only take one string as an argument
// I have no plans to change this right now, but only time will tell.
std::optional<std::string> stringArgument(slang::UserAttribute *attribute) {
  if (attribute->getArgumentCount() != 1) {
    return std::nullopt;
  }
  size_t size = 0;
  const char *text = attribute->getArgumentValueString(0, &size);
  if (!text) {
    return std::nullopt;
  }
  return std::string(text, size);
}

// Reads the strafe attributes (defined in shaders/strafe/attributes.slang)
bool readAttributes(slang::EntryPointReflection *entry_point,
                    ShaderReflection &reflection,
                    std::unordered_map<std::string, std::string> &seen) {
  slang::FunctionReflection *function = entry_point->getFunction();
  if (!function) {
    return true;
  }
  const std::string_view shader = reflection.name;

  for (unsigned i = 0; i < function->getUserAttributeCount(); ++i) {
    slang::UserAttribute *attribute = function->getUserAttributeByIndex(i);
    const std::string name = attribute->getName();
    const std::optional<std::string> value = stringArgument(attribute);
    if (!value) {
      log::error("Shader {}: [{}] on {} needs one string argument", shader,
                 name, entry_point->getName());
      return false;
    }
    if (const auto it = seen.find(name); it != seen.end()) {
      if (it->second != *value) {
        log::error("Shader {}: conflicting [{}(\"{}\")] and [{}(\"{}\")]",
                   shader, name, it->second, name, *value);
        return false;
      }
      continue;
    }
    seen.emplace(name, *value);

    bool valid = true;
    if (name == "Blend") {
      const std::optional<BlendMode> mode = parseBlendMode(*value);
      valid = mode.has_value();
      reflection.blend = mode.value_or(reflection.blend);
    } else if (name == "Cull") {
      const std::optional<CullMode> mode = parseCullMode(*value);
      valid = mode.has_value();
      reflection.cull = mode.value_or(reflection.cull);
    } else if (name == "Depth") {
      const std::optional<DepthMode> mode = parseDepthMode(*value);
      valid = mode.has_value();
      reflection.depth = mode.value_or(reflection.depth);
    } else if (name == "Pass") {
      reflection.pass = *value;
    } else if (name == "Material") {
      reflection.material_type = *value;
    } else {
      log::error("Shader {}: unknown attribute [{}]", shader, name);
      return false;
    }
    if (!valid) {
      log::error("Shader {}: [{}(\"{}\")] is not a valid value, see "
                 "strafe/attributes.slang",
                 shader, name, *value);
      return false;
    }
  }
  return true;
}

bool isColorTarget(const char *semantic) {
  if (!semantic) {
    return false;
  }
  constexpr std::string_view kTarget = "SV_TARGET";
  const std::string_view text = semantic;
  if (text.size() < kTarget.size()) {
    return false;
  }
  for (std::size_t i = 0; i < kTarget.size(); ++i) {
    if (std::toupper(static_cast<unsigned char>(text[i])) != kTarget[i]) {
      return false;
    }
  }
  return true;
}

// SV_Target outputs
uint32_t countColorOutputs(slang::EntryPointReflection *entry_point) {
  slang::VariableLayoutReflection *result = entry_point->getResultVarLayout();
  if (!result) {
    return 0;
  }
  if (isColorTarget(result->getSemanticName())) {
    return 1;
  }
  slang::TypeLayoutReflection *type = result->getTypeLayout();
  if (!type || type->getKind() != slang::TypeReflection::Kind::Struct) {
    return 0;
  }
  uint32_t count = 0;
  for (unsigned i = 0; i < type->getFieldCount(); ++i) {
    if (isColorTarget(type->getFieldByIndex(i)->getSemanticName())) {
      ++count;
    }
  }
  return count;
}

uint32_t pushConstantSize(slang::ProgramLayout *layout) {
  for (unsigned i = 0; i < layout->getParameterCount(); ++i) {
    slang::VariableLayoutReflection *parameter = layout->getParameterByIndex(i);
    if (parameter->getCategory() ==
        slang::ParameterCategory::PushConstantBuffer) {
      return static_cast<uint32_t>(
          parameter->getTypeLayout()->getElementTypeLayout()->getSize());
    }
  }
  return 0;
}

} // namespace

ShaderCompiler::~ShaderCompiler() { shutdown(); }

bool ShaderCompiler::initialize(std::string search_path) {
  search_path_ = std::move(search_path);
  if (SLANG_FAILED(slang::createGlobalSession(&global_session_))) {
    log::error("Failed to create the Slang global session");
    return false;
  }
  return true;
}

void ShaderCompiler::shutdown() {
  if (global_session_) {
    global_session_->release();
    global_session_ = nullptr;
  }
}

std::optional<CompiledShader>
ShaderCompiler::compile(std::string_view module_name) {
  const slang::TargetDesc target{.format = SLANG_SPIRV,
                                 .profile =
                                     global_session_->findProfile("spirv_1_6")};

  auto int_option = [](slang::CompilerOptionName name, int value) {
    return slang::CompilerOptionEntry{
        .name = name,
        .value = {.kind = slang::CompilerOptionValueKind::Int,
                  .intValue0 = value}};
  };
  const std::array options{
      int_option(slang::CompilerOptionName::GLSLForceScalarLayout, 1),
      int_option(slang::CompilerOptionName::VulkanUseEntryPointName, 1),
      slang::CompilerOptionEntry{
          .name = slang::CompilerOptionName::DisableWarning,
          .value = {.kind = slang::CompilerOptionValueKind::String,
                    .stringValue0 = "41012"}},
#ifdef STRAFE_DEBUG
      // Source level debugging in RenderDoc.
      int_option(slang::CompilerOptionName::DebugInformation,
                 SLANG_DEBUG_INFO_LEVEL_STANDARD),
      int_option(slang::CompilerOptionName::Optimization,
                 SLANG_OPTIMIZATION_LEVEL_DEFAULT),
#else
      int_option(slang::CompilerOptionName::Optimization,
                 SLANG_OPTIMIZATION_LEVEL_HIGH),
#endif
  };

  const char *search_paths[] = {search_path_.c_str()};
  const slang::SessionDesc session_desc{
      .targets = &target,
      .targetCount = 1,
      .defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR,
      .searchPaths = search_paths,
      .searchPathCount = 1,
      .compilerOptionEntries =
          const_cast<slang::CompilerOptionEntry *>(options.data()),
      .compilerOptionEntryCount = static_cast<uint32_t>(options.size())};

  Slang::ComPtr<slang::ISession> session;
  if (SLANG_FAILED(
          global_session_->createSession(session_desc, session.writeRef()))) {
    log::error("Failed to create a Slang session");
    return std::nullopt;
  }

  const std::string name(module_name);
  log::info("Compiling shader: {}", name);

  Slang::ComPtr<slang::IBlob> diagnostics;
  slang::IModule *module =
      session->loadModule(name.c_str(), diagnostics.writeRef());
  logDiagnostics(diagnostics, module == nullptr);
  if (!module) {
    return std::nullopt;
  }

  // The module itself, then every entry point it defines.
  std::vector<Slang::ComPtr<slang::IComponentType>> components{
      Slang::ComPtr<slang::IComponentType>(module)};
  const SlangInt32 entry_point_count = module->getDefinedEntryPointCount();
  if (entry_point_count == 0) {
    log::error("Shader {} has no [shader(...)] entry points", name);
    return std::nullopt;
  }
  for (SlangInt32 i = 0; i < entry_point_count; ++i) {
    Slang::ComPtr<slang::IEntryPoint> entry_point;
    module->getDefinedEntryPoint(i, entry_point.writeRef());
    components.emplace_back(entry_point.get());
  }

  std::vector<slang::IComponentType *> raw_components;
  for (const auto &component : components) {
    raw_components.push_back(component.get());
  }
  Slang::ComPtr<slang::IComponentType> composite;
  if (SLANG_FAILED(session->createCompositeComponentType(
          raw_components.data(), static_cast<SlangInt>(raw_components.size()),
          composite.writeRef(), diagnostics.writeRef()))) {
    logDiagnostics(diagnostics, true);
    return std::nullopt;
  }

  Slang::ComPtr<slang::IComponentType> linked;
  const bool link_failed =
      SLANG_FAILED(composite->link(linked.writeRef(), diagnostics.writeRef()));
  logDiagnostics(diagnostics, link_failed);
  if (link_failed) {
    return std::nullopt;
  }

  slang::ProgramLayout *layout = linked->getLayout();
  CompiledShader shader;
  shader.reflection.name = name;
  shader.reflection.push_constant_size = pushConstantSize(layout);
  std::unordered_map<std::string, std::string> seen_attributes;

  for (SlangInt32 i = 0; i < entry_point_count; ++i) {
    slang::EntryPointReflection *reflection = layout->getEntryPointByIndex(i);
    const std::optional<VkShaderStageFlagBits> stage =
        toVkStage(reflection->getStage());
    if (!stage) {
      log::error("Shader {}: entry point {} has an unsupported stage", name,
                 reflection->getName());
      return std::nullopt;
    }
    if (!readAttributes(reflection, shader.reflection, seen_attributes)) {
      return std::nullopt;
    }
    if (*stage == VK_SHADER_STAGE_FRAGMENT_BIT) {
      shader.reflection.color_outputs = countColorOutputs(reflection);
    }

    Slang::ComPtr<slang::IBlob> code;
    const bool code_failed = SLANG_FAILED(linked->getEntryPointCode(
        i, 0, code.writeRef(), diagnostics.writeRef()));
    logDiagnostics(diagnostics, code_failed);
    if (code_failed) {
      return std::nullopt;
    }

    CompiledStage compiled{.stage = *stage,
                           .entry_point = reflection->getName()};
    compiled.spirv.resize(code->getBufferSize() / sizeof(uint32_t));
    std::memcpy(compiled.spirv.data(), code->getBufferPointer(),
                compiled.spirv.size() * sizeof(uint32_t));
    shader.stages.push_back(std::move(compiled));
  }
  return shader;
}

} // namespace strafe::gfx
