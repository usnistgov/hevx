#include "renderer/shader.h"
#include "logging.h"
#include "renderer/impl.h"
#include "renderer/io/read_file.h"
#if PLATFORM_COMPILER_GCC
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#endif
#include "glslang/Public/ShaderLang.h"
#include "SPIRV/GlslangToSpv.h"
#include "SPIRV/GLSL.std.450.h"
#if PLATFORM_COMPILER_GCC
#pragma GCC diagnostic pop
#endif

namespace iris::Renderer {

const TBuiltInResource DefaultTBuiltInResource = {
  /* .MaxLights = */ 32,
  /* .MaxClipPlanes = */ 6,
  /* .MaxTextureUnits = */ 32,
  /* .MaxTextureCoords = */ 32,
  /* .MaxVertexAttribs = */ 64,
  /* .MaxVertexUniformComponents = */ 4096,
  /* .MaxVaryingFloats = */ 64,
  /* .MaxVertexTextureImageUnits = */ 32,
  /* .MaxCombinedTextureImageUnits = */ 80,
  /* .MaxTextureImageUnits = */ 32,
  /* .MaxFragmentUniformComponents = */ 4096,
  /* .MaxDrawBuffers = */ 32,
  /* .MaxVertexUniformVectors = */ 128,
  /* .MaxVaryingVectors = */ 8,
  /* .MaxFragmentUniformVectors = */ 16,
  /* .MaxVertexOutputVectors = */ 16,
  /* .MaxFragmentInputVectors = */ 15,
  /* .MinProgramTexelOffset = */ -8,
  /* .MaxProgramTexelOffset = */ 7,
  /* .MaxClipDistances = */ 8,
  /* .MaxComputeWorkGroupCountX = */ 65535,
  /* .MaxComputeWorkGroupCountY = */ 65535,
  /* .MaxComputeWorkGroupCountZ = */ 65535,
  /* .MaxComputeWorkGroupSizeX = */ 1024,
  /* .MaxComputeWorkGroupSizeY = */ 1024,
  /* .MaxComputeWorkGroupSizeZ = */ 64,
  /* .MaxComputeUniformComponents = */ 1024,
  /* .MaxComputeTextureImageUnits = */ 16,
  /* .MaxComputeImageUniforms = */ 8,
  /* .MaxComputeAtomicCounters = */ 8,
  /* .MaxComputeAtomicCounterBuffers = */ 1,
  /* .MaxVaryingComponents = */ 60,
  /* .MaxVertexOutputComponents = */ 64,
  /* .MaxGeometryInputComponents = */ 64,
  /* .MaxGeometryOutputComponents = */ 128,
  /* .MaxFragmentInputComponents = */ 128,
  /* .MaxImageUnits = */ 8,
  /* .MaxCombinedImageUnitsAndFragmentOutputs = */ 8,
  /* .MaxCombinedShaderOutputResources = */ 8,
  /* .MaxImageSamples = */ 0,
  /* .MaxVertexImageUniforms = */ 0,
  /* .MaxTessControlImageUniforms = */ 0,
  /* .MaxTessEvaluationImageUniforms = */ 0,
  /* .MaxGeometryImageUniforms = */ 0,
  /* .MaxFragmentImageUniforms = */ 8,
  /* .MaxCombinedImageUniforms = */ 8,
  /* .MaxGeometryTextureImageUnits = */ 16,
  /* .MaxGeometryOutputVertices = */ 256,
  /* .MaxGeometryTotalOutputComponents = */ 1024,
  /* .MaxGeometryUniformComponents = */ 1024,
  /* .MaxGeometryVaryingComponents = */ 64,
  /* .MaxTessControlInputComponents = */ 128,
  /* .MaxTessControlOutputComponents = */ 128,
  /* .MaxTessControlTextureImageUnits = */ 16,
  /* .MaxTessControlUniformComponents = */ 1024,
  /* .MaxTessControlTotalOutputComponents = */ 4096,
  /* .MaxTessEvaluationInputComponents = */ 128,
  /* .MaxTessEvaluationOutputComponents = */ 128,
  /* .MaxTessEvaluationTextureImageUnits = */ 16,
  /* .MaxTessEvaluationUniformComponents = */ 1024,
  /* .MaxTessPatchComponents = */ 120,
  /* .MaxPatchVertices = */ 32,
  /* .MaxTessGenLevel = */ 64,
  /* .MaxViewports = */ 16,
  /* .MaxVertexAtomicCounters = */ 0,
  /* .MaxTessControlAtomicCounters = */ 0,
  /* .MaxTessEvaluationAtomicCounters = */ 0,
  /* .MaxGeometryAtomicCounters = */ 0,
  /* .MaxFragmentAtomicCounters = */ 8,
  /* .MaxCombinedAtomicCounters = */ 8,
  /* .MaxAtomicCounterBindings = */ 1,
  /* .MaxVertexAtomicCounterBuffers = */ 0,
  /* .MaxTessControlAtomicCounterBuffers = */ 0,
  /* .MaxTessEvaluationAtomicCounterBuffers = */ 0,
  /* .MaxGeometryAtomicCounterBuffers = */ 0,
  /* .MaxFragmentAtomicCounterBuffers = */ 1,
  /* .MaxCombinedAtomicCounterBuffers = */ 1,
  /* .MaxAtomicCounterBufferSize = */ 16384,
  /* .MaxTransformFeedbackBuffers = */ 4,
  /* .MaxTransformFeedbackInterleavedComponents = */ 64,
  /* .MaxCullDistances = */ 8,
  /* .MaxCombinedClipAndCullDistances = */ 8,
  /* .MaxSamples = */ 4,
  /* .maxMeshOutputVerticesNV = */ 256,
  /* .maxMeshOutputPrimitivesNV = */ 512,
  /* .maxMeshWorkGroupSizeX_NV = */ 32,
  /* .maxMeshWorkGroupSizeY_NV = */ 1,
  /* .maxMeshWorkGroupSizeZ_NV = */ 1,
  /* .maxTaskWorkGroupSizeX_NV = */ 32,
  /* .maxTaskWorkGroupSizeY_NV = */ 1,
  /* .maxTaskWorkGroupSizeZ_NV = */ 1,
  /* .maxMeshViewCountNV = */ 4,

  /* .limits = */
  {
    /* .nonInductiveForLoops = */ true,
    /* .whileLoops = */ true,
    /* .doWhileLoops = */ true,
    /* .generalUniformIndexing = */ true,
    /* .generalAttributeMatrixVectorIndexing = */ true,
    /* .generalVaryingIndexing = */ true,
    /* .generalSamplerIndexing = */ true,
    /* .generalVariableIndexing = */ true,
    /* .generalConstantMatrixVectorIndexing = */ true,
  }};

[[nodiscard]] static tl::expected<std::vector<std::uint32_t>, std::string>
CompileShaderFromSource(std::string_view source,
                        VkShaderStageFlagBits shaderStage,
                        filesystem::path const& path,
                        gsl::span<std::string> macroDefinitions[[maybe_unused]],
                        std::string const& entryPoint) {
  IRIS_LOG_ENTER();
  Expects(source.size() > 0);

  auto const lang = [&shaderStage]() {
    if ((shaderStage & VK_SHADER_STAGE_VERTEX_BIT)) {
      return EShLanguage::EShLangVertex;
    } else if ((shaderStage & VK_SHADER_STAGE_FRAGMENT_BIT)) {
      return EShLanguage::EShLangFragment;
    } else {
      GetLogger()->critical("Unhandled shaderStage: {}", shaderStage);
      std::terminate();
    }
  }();

  char const* strings[] = {source.data()};
  int lengths[] = {static_cast<int>(source.size())};
  char const* names[] = {path.string().c_str()};

  glslang::TShader shader(lang);
  shader.setStringsWithLengthsAndNames(strings, lengths, names, 1);
  shader.setEntryPoint(entryPoint.c_str());
  shader.setEnvInput(glslang::EShSource::EShSourceGlsl, lang,
                     glslang::EShClient::EShClientVulkan, 101);
  shader.setEnvClient(glslang::EShClient::EShClientVulkan,
                      glslang::EShTargetClientVersion::EShTargetVulkan_1_1);
  shader.setEnvTarget(glslang::EShTargetLanguage::EShTargetSpv,
                      glslang::EShTargetLanguageVersion::EShTargetSpv_1_0);

  if (!shader.parse(&DefaultTBuiltInResource, 1, false,
                    EShMessages::EShMsgDefault)) {
    return tl::unexpected(std::string(shader.getInfoLog()));
  }

  glslang::TProgram program;
  program.addShader(&shader);

  if (!program.link(EShMessages::EShMsgDefault)) {
    return tl::unexpected(std::string(program.getInfoLog()));
  }

  if (auto glsl = program.getIntermediate(lang)) {
    glslang::SpvOptions options;
    options.validate = true;
#ifndef NDEBUG
    options.generateDebugInfo = true;
#endif

    spv::SpvBuildLogger logger;
    std::vector<std::uint32_t> code;
    glslang::GlslangToSpv(*glsl, code, &logger, &options);

    Ensures(code.size() > 0);
    IRIS_LOG_LEAVE();
    return code;
  } else {
    return tl::unexpected(std::string(
      "cannot get glsl intermediate representation of compiled shader"));
  }
} // CompileShaderFromSource

} // namespace iris::Renderer

tl::expected<iris::Renderer::Shader, std::system_error>
iris::Renderer::Shader::CreateFromSource(
  std::string_view source, VkShaderStageFlagBits stage,
  gsl::span<std::string> macroDefinitions, std::string entry,
  std::string name) noexcept {
  IRIS_LOG_ENTER();
  Expects(sDevice != VK_NULL_HANDLE);
  Expects(source.size() > 0);

  Shader shader;

  std::vector<std::uint32_t> code;
  if (auto c = CompileShaderFromSource(source, stage, "<inline>",
                                       macroDefinitions, entry)) {
    code = std::move(*c);
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(Error::kShaderCompileFailed, c.error()));
  }

  VkShaderModuleCreateInfo smci = {};
  smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  // codeSize is in bytes, not count of words (which is what vector.size() is)
  smci.codeSize = gsl::narrow_cast<std::uint32_t>(code.size() * 4);
  smci.pCode = code.data();

  if (auto result =
        vkCreateShaderModule(sDevice, &smci, nullptr, &shader.handle);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(make_error_code(result),
                                            "Cannot create shader module"));
  }

  if (!name.empty()) {
    NameObject(VK_OBJECT_TYPE_SHADER_MODULE, shader.handle, name.c_str());
  }

  shader.stage = stage;
  shader.entry = std::move(entry);
  shader.name = std::move(name);

  Ensures(shader.handle != VK_NULL_HANDLE);
  IRIS_LOG_LEAVE();
  return std::move(shader);
} // iris::Renderer::Shader::CreateFromSource

tl::expected<iris::Renderer::Shader, std::system_error>
iris::Renderer::Shader::CreateFromFile(filesystem::path const& path,
                                       VkShaderStageFlagBits stage,
                                       gsl::span<std::string> macroDefinitions,
                                       std::string entry,
                                       std::string name) noexcept {
  IRIS_LOG_ENTER();
  Expects(sDevice != VK_NULL_HANDLE);
  Expects(!path.empty());

  Shader shader;

  std::vector<std::byte> source;
  if (auto s = io::ReadFile(path)) {
    source = std::move(*s);
  } else {
    return tl::unexpected(s.error());
  }

  std::vector<std::uint32_t> code;
  if (auto c = CompileShaderFromSource(
        {reinterpret_cast<char*>(source.data()), source.size()}, stage, path,
        macroDefinitions, entry)) {
    code = std::move(*c);
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(Error::kShaderCompileFailed, c.error()));
  }

  VkShaderModuleCreateInfo smci = {};
  smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  // codeSize is in bytes, not count of words (which is what vector.size() is)
  smci.codeSize = gsl::narrow_cast<std::uint32_t>(code.size() * 4);
  smci.pCode = code.data();

  if (auto result =
        vkCreateShaderModule(sDevice, &smci, nullptr, &shader.handle);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(make_error_code(result),
                                            "Cannot create shader module"));
  }

  if (!name.empty()) {
    NameObject(VK_OBJECT_TYPE_SHADER_MODULE, shader.handle, name.c_str());
  }

  shader.stage = stage;
  shader.entry = std::move(entry);
  shader.name = std::move(name);

  Ensures(shader.handle != VK_NULL_HANDLE);
  IRIS_LOG_LEAVE();
  return std::move(shader);
} // iris::Renderer::Shader::CreateFromFile

iris::Renderer::Shader::Shader(Shader&& other) noexcept
  : stage(other.stage)
  , handle(other.handle)
  , entry(std::move(other.entry))
  , name(std::move(other.name)) {
  other.handle = nullptr;
} // iris::Renderer::Shader::Shader

iris::Renderer::Shader& iris::Renderer::Shader::operator=(Shader&& rhs) noexcept {
  if (this == &rhs) return *this;

  stage = rhs.stage;
  handle = rhs.handle;
  entry = std::move(rhs.entry);
  name = std::move(rhs.name);

  rhs.handle = VK_NULL_HANDLE;

  return *this;
} // iris::Renderer::Shader::operator=

iris::Renderer::Shader::~Shader() noexcept {
  if (handle == VK_NULL_HANDLE) return;
  IRIS_LOG_ENTER();
  Expects(sDevice != VK_NULL_HANDLE);

  vkDestroyShaderModule(sDevice, handle, nullptr);

  IRIS_LOG_LEAVE();
} // iris::Renderer::~Shader

