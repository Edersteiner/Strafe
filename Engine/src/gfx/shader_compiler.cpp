#include "strafe/gfx/shader_compiler.h"

#include "strafe/core/log.h"

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
  const std::string_view text(static_cast<const char *>(diagnostics->getBufferPointer()), diagnostics->getBufferSize());
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
bool readAttributes(
    slang::EntryPointReflection *entry_point, ShaderReflection &reflection,
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
      log::error("Shader {}: [{}] on {} needs one string argument", shader, name, entry_point->getName());
      return false;
    }
    if (const auto it = seen.find(name); it != seen.end()) {
      if (it->second != *value) {
        log::error("Shader {}: conflicting [{}(\"{}\")] and [{}(\"{}\")]", shader, name, it->second, name, *value);
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
      log::error("Shader {}: [{}(\"{}\")] is not a valid value, see strafe/attributes.slang", shader, name, *value);
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

// The struct inside the push constant block, null when the shader has none.
slang::TypeLayoutReflection *pushConstantType(slang::ProgramLayout *layout) {
  for (unsigned i = 0; i < layout->getParameterCount(); ++i) {
    slang::VariableLayoutReflection *parameter = layout->getParameterByIndex(i);
    if (parameter->getCategory() == slang::ParameterCategory::PushConstantBuffer) {
      return parameter->getTypeLayout()->getElementTypeLayout();
    }
  }
  return nullptr;
}

// Size of the struct a pointer field points to, 0 when there is no such
// pointer field.
uint32_t pointeeSize(slang::TypeLayoutReflection *type, std::string_view field) {
  if (!type) {
    return 0;
  }
  for (unsigned i = 0; i < type->getFieldCount(); ++i) {
    slang::VariableLayoutReflection *candidate = type->getFieldByIndex(i);
    if (candidate->getName() && field == candidate->getName() &&
        candidate->getTypeLayout()->getKind() == slang::TypeReflection::Kind::Pointer) {
      return static_cast<uint32_t>(candidate->getTypeLayout()->getElementTypeLayout()->getSize());
    }
  }
  return 0;
}

std::optional<MaterialFieldType> materialFieldType(slang::TypeLayoutReflection *type) {
  using Kind = slang::TypeReflection::Kind;
  switch (type->getKind()) {
  case Kind::Scalar:
    switch (type->getScalarType()) {
    case slang::TypeReflection::ScalarType::Float32:
      return MaterialFieldType::Float;
    case slang::TypeReflection::ScalarType::Int32:
      return MaterialFieldType::Int;
    case slang::TypeReflection::ScalarType::UInt32:
      return MaterialFieldType::UInt;
    default:
      return std::nullopt;
    }
  case Kind::Vector:
    if (type->getElementTypeLayout()->getScalarType() != slang::TypeReflection::ScalarType::Float32) {
      return std::nullopt;
    }
    switch (type->getElementCount()) {
    case 2:
      return MaterialFieldType::Float2;
    case 3:
      return MaterialFieldType::Float3;
    case 4:
      return MaterialFieldType::Float4;
    default:
      return std::nullopt;
    }
  case Kind::Struct: {
    const char *name = type->getName();
    if (!name) {
      return std::nullopt;
    }
    if (std::string_view(name) == "Texture") {
      return MaterialFieldType::Texture;
    }
    if (std::string_view(name) == "Sampler") {
      return MaterialFieldType::Sampler;
    }
    return std::nullopt;
  }
  default:
    return std::nullopt;
  }
}

constexpr size_t fieldSize(MaterialFieldType type) {
  switch (type) {
  case MaterialFieldType::Float2:
    return 8;
  case MaterialFieldType::Float3:
    return 12;
  case MaterialFieldType::Float4:
    return 16;
  default:
    return 4;
  }
}

// The field's initializer, e.g. `float4 tint = float4(1.0);`. Empty when it
// has none or Slang can't evaluate it.
std::vector<std::byte> defaultValue(slang::VariableLayoutReflection *field, MaterialFieldType type) {
  slang::VariableReflection *variable = field->getVariable();
  Slang::ComPtr<ISlangBlob> blob;
  if (!variable || SLANG_FAILED(variable->getDefaultValueBlob(blob.writeRef())) || !blob) {
    return {};
  }
  // Packed without padding, so for these types it is exactly the GPU bytes.
  if (blob->getBufferSize() != fieldSize(type)) {
    log::warn(
        "Material field {} has a default of {} bytes, expected {}. Ignoring it", field->getName(),
        blob->getBufferSize(), fieldSize(type));
    return {};
  }
  const auto *bytes = static_cast<const std::byte *>(blob->getBufferPointer());
  return {bytes, bytes + blob->getBufferSize()};
}

// Fills reflection.material from the struct named by [Material("...")].
bool reflectMaterial(slang::ProgramLayout *layout, ShaderReflection &reflection) {
  const std::string &type_name = reflection.material_type;
  slang::TypeReflection *type = layout->findTypeByName(type_name.c_str());
  if (!type) {
    log::error("Shader {}: [Material(\"{}\")] names no type in the shader", reflection.name, type_name);
    return false;
  }

  const std::string pointer_name = "Ptr<" + type_name + ">";
  slang::TypeReflection *pointer = layout->findTypeByName(pointer_name.c_str());
  slang::TypeLayoutReflection *pointer_layout = pointer ? layout->getTypeLayout(pointer) : nullptr;
  slang::TypeLayoutReflection *type_layout = pointer_layout ? pointer_layout->getElementTypeLayout() : nullptr;
  if (!type_layout || type_layout->getKind() != slang::TypeReflection::Kind::Struct) {
    log::error("Shader {}: [Material(\"{}\")] must name a struct", reflection.name, type_name);
    return false;
  }

  MaterialLayout &material = reflection.material;
  for (unsigned i = 0; i < type_layout->getFieldCount(); ++i) {
    slang::VariableLayoutReflection *field = type_layout->getFieldByIndex(i);
    const std::optional<MaterialFieldType> field_type = materialFieldType(field->getTypeLayout());
    if (!field_type) {
      log::error(
          "Shader {}: {}.{} has a type materials don't support. Use float, float2-4, int, uint, Texture or Sampler",
          reflection.name, type_name, field->getName());
      return false;
    }
    material.fields.push_back(
        {.name = field->getName(),
         .type = *field_type,
         .offset = static_cast<uint32_t>(field->getOffset()),
         .default_value = defaultValue(field, *field_type)});
  }
  material.size = static_cast<uint32_t>(type_layout->getSize());
  return true;
}

} // namespace

ShaderCompiler::~ShaderCompiler() { shutdown(); }

bool ShaderCompiler::initialize(std::vector<std::string> search_paths) {
  search_paths_ = std::move(search_paths);
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

std::optional<CompiledShader> ShaderCompiler::compile(std::string_view module_name) {
  const slang::TargetDesc target{.format = SLANG_SPIRV, .profile = global_session_->findProfile("spirv_1_6")};

  auto int_option = [](slang::CompilerOptionName name, int value) {
    return slang::CompilerOptionEntry{
        .name = name, .value = {.kind = slang::CompilerOptionValueKind::Int, .intValue0 = value}};
  };
  const std::array options{
      int_option(slang::CompilerOptionName::GLSLForceScalarLayout, 1),
      int_option(slang::CompilerOptionName::VulkanUseEntryPointName, 1),
      slang::CompilerOptionEntry{
          .name = slang::CompilerOptionName::DisableWarning,
          .value = {.kind = slang::CompilerOptionValueKind::String, .stringValue0 = "41012"}},
#ifdef STRAFE_DEBUG
      // Source level debugging in RenderDoc.
      int_option(slang::CompilerOptionName::DebugInformation, SLANG_DEBUG_INFO_LEVEL_STANDARD),
      int_option(slang::CompilerOptionName::Optimization, SLANG_OPTIMIZATION_LEVEL_DEFAULT),
#else
      int_option(slang::CompilerOptionName::Optimization, SLANG_OPTIMIZATION_LEVEL_HIGH),
#endif
  };

  std::vector<const char *> search_paths;
  for (const std::string &search_path : search_paths_) {
    search_paths.push_back(search_path.c_str());
  }
  const slang::SessionDesc session_desc{
      .targets = &target,
      .targetCount = 1,
      .defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR,
      .searchPaths = search_paths.data(),
      .searchPathCount = static_cast<SlangInt>(search_paths.size()),
      .compilerOptionEntries = const_cast<slang::CompilerOptionEntry *>(options.data()),
      .compilerOptionEntryCount = static_cast<uint32_t>(options.size())};

  Slang::ComPtr<slang::ISession> session;
  if (SLANG_FAILED(global_session_->createSession(session_desc, session.writeRef()))) {
    log::error("Failed to create a Slang session");
    return std::nullopt;
  }

  const std::string name(module_name);
  log::info("Compiling shader: {}", name);

  Slang::ComPtr<slang::IBlob> diagnostics;
  slang::IModule *module = session->loadModule(name.c_str(), diagnostics.writeRef());
  logDiagnostics(diagnostics, module == nullptr);
  if (!module) {
    return std::nullopt;
  }

  // The module itself, then every entry point it defines.
  std::vector<Slang::ComPtr<slang::IComponentType>> components{Slang::ComPtr<slang::IComponentType>(module)};
  const SlangInt32 defined_count = module->getDefinedEntryPointCount();
  if (defined_count == 0) {
    log::error("Shader {} has no [shader(...)] entry points", name);
    return std::nullopt;
  }
  bool has_vertex = false;
  for (SlangInt32 i = 0; i < defined_count; ++i) {
    Slang::ComPtr<slang::IEntryPoint> entry_point;
    module->getDefinedEntryPoint(i, entry_point.writeRef());
    slang::ProgramLayout *entry_layout = entry_point->getLayout();
    if (entry_layout && entry_layout->getEntryPointCount() > 0 &&
        entry_layout->getEntryPointByIndex(0)->getStage() == SLANG_STAGE_VERTEX) {
      has_vertex = true;
    }
    components.emplace_back(entry_point.get());
  }

  // A shader without its own vertex stage gets the standard one, which
  // outputs the Surface every fragment shader in the library takes.
  if (!has_vertex) {
    slang::IModule *standard = session->loadModule("strafe/standard_vertex", diagnostics.writeRef());
    logDiagnostics(diagnostics, standard == nullptr);
    Slang::ComPtr<slang::IEntryPoint> entry_point;
    if (!standard || SLANG_FAILED(standard->findEntryPointByName("standardVertexMain", entry_point.writeRef()))) {
      log::error("Shader {}: failed to load the standard vertex shader", name);
      return std::nullopt;
    }
    components.emplace_back(Slang::ComPtr<slang::IComponentType>(standard));
    components.emplace_back(entry_point.get());
  }

  std::vector<slang::IComponentType *> raw_components;
  for (const auto &component : components) {
    raw_components.push_back(component.get());
  }
  Slang::ComPtr<slang::IComponentType> composite;
  if (SLANG_FAILED(session->createCompositeComponentType(
          raw_components.data(), static_cast<SlangInt>(raw_components.size()), composite.writeRef(),
          diagnostics.writeRef()))) {
    logDiagnostics(diagnostics, true);
    return std::nullopt;
  }

  Slang::ComPtr<slang::IComponentType> linked;
  const bool link_failed = SLANG_FAILED(composite->link(linked.writeRef(), diagnostics.writeRef()));
  logDiagnostics(diagnostics, link_failed);
  if (link_failed) {
    return std::nullopt;
  }

  slang::ProgramLayout *layout = linked->getLayout();
  CompiledShader shader;
  shader.reflection.name = name;
  slang::TypeLayoutReflection *push_type = pushConstantType(layout);
  shader.reflection.push_constant_size = push_type ? static_cast<uint32_t>(push_type->getSize()) : 0;
  shader.reflection.frame_data_size = pointeeSize(push_type, "frame");
  std::unordered_map<std::string, std::string> seen_attributes;

  const auto entry_point_count = static_cast<SlangInt32>(layout->getEntryPointCount());
  for (SlangInt32 i = 0; i < entry_point_count; ++i) {
    slang::EntryPointReflection *reflection = layout->getEntryPointByIndex(i);
    const std::optional<VkShaderStageFlagBits> stage = toVkStage(reflection->getStage());
    if (!stage) {
      log::error("Shader {}: entry point {} has an unsupported stage", name, reflection->getName());
      return std::nullopt;
    }
    if (!readAttributes(reflection, shader.reflection, seen_attributes)) {
      return std::nullopt;
    }
    if (*stage == VK_SHADER_STAGE_FRAGMENT_BIT) {
      shader.reflection.color_outputs = countColorOutputs(reflection);
    }

    Slang::ComPtr<slang::IBlob> code;
    const bool code_failed = SLANG_FAILED(linked->getEntryPointCode(i, 0, code.writeRef(), diagnostics.writeRef()));
    logDiagnostics(diagnostics, code_failed);
    if (code_failed) {
      return std::nullopt;
    }

    CompiledStage compiled{.stage = *stage, .entry_point = reflection->getName()};
    compiled.spirv.resize(code->getBufferSize() / sizeof(uint32_t));
    std::memcpy(compiled.spirv.data(), code->getBufferPointer(), compiled.spirv.size() * sizeof(uint32_t));
    shader.stages.push_back(std::move(compiled));
  }

  if (!shader.reflection.material_type.empty() && !reflectMaterial(layout, shader.reflection)) {
    return std::nullopt;
  }
  return shader;
}

} // namespace strafe::gfx
