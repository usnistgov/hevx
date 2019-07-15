#include "shader.h"
#include "config.h"

#include "error.h"
#include "io/read_file.h"
#include "logging.h"
#include "renderer_private.h"

#if PLATFORM_COMPILER_GCC
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#endif
#include "SPIRV/GLSL.std.450.h"
#include "SPIRV/GlslangToSpv.h"
#include "glslang/Public/ShaderLang.h"
#if PLATFORM_COMPILER_GCC
#pragma GCC diagnostic pop
#endif

#include <fstream>

namespace iris {

using namespace std::string_literals;

static const TBuiltInResource DefaultTBuiltInResource = {
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

class DirStackIncluder : public glslang::TShader::Includer {
public:
  DirStackIncluder() noexcept = default;

  virtual IncludeResult* includeLocal(char const* headerName,
                                      char const* includerName,
                                      std::size_t inclusionDepth) override {
    return readLocalPath(headerName, includerName,
                         gsl::narrow_cast<int>(inclusionDepth));
  }

  virtual IncludeResult* includeSystem(char const* headerName,
                                       char const* includerName
                                       [[maybe_unused]],
                                       std::size_t inclusionDepth
                                       [[maybe_unused]]) override {
    return readSystemPath(headerName);
  }

  virtual void releaseInclude(IncludeResult* result) override {
    if (result) {
      delete[] static_cast<char*>(result->userData);
      delete result;
    }
  }

  virtual void pushExternalLocalDirectory(std::string const& dir) {
    dirStack_.push_back(dir);
    numExternalLocalDirs_ = gsl::narrow_cast<int>(dirStack_.size());
  }

private:
  std::vector<std::string> dirStack_{};
  int numExternalLocalDirs_{0};

  virtual IncludeResult* readLocalPath(std::string const& headerName,
                                       std::string const& includerName,
                                       int depth) {
    // Discard popped include directories, and
    // initialize when at parse-time first level.
    dirStack_.resize(depth + numExternalLocalDirs_);

    if (depth == 1) dirStack_.back() = getDirectory(includerName);

    // Find a directory that works, using a reverse search of the include stack.
    for (auto& dir : dirStack_) {
      std::string path = dir + "/"s + headerName;
      std::replace(path.begin(), path.end(), '\\', '/');
      std::ifstream ifs(path.c_str(),
                        std::ios_base::binary | std::ios_base::ate);
      if (ifs) {
        dirStack_.push_back(getDirectory(path));
        return newIncludeResult(path, ifs, gsl::narrow_cast<int>(ifs.tellg()));
      }
    }

    return nullptr;
  }

  virtual IncludeResult* readSystemPath(char const*) const {
    IRIS_LOG_ERROR("including system headers not implemented");
    return nullptr;
  }

  virtual IncludeResult* newIncludeResult(std::string const& path,
                                          std::ifstream& ifs,
                                          int length) const {
    char* content = new char[length];
    ifs.seekg(0, ifs.beg);
    ifs.read(content, length);
    return new IncludeResult(path.c_str(), content, length, content);
  }

  // If no path markers, return current working directory.
  // Otherwise, strip file name and return path leading up to it.
  virtual std::string getDirectory(const std::string path) const {
    size_t last = path.find_last_of("/\\");
    return last == std::string::npos ? "." : path.substr(0, last);
  }
}; // class DirStackIncluder

[[nodiscard]] static tl::expected<std::vector<std::uint32_t>, std::string>
CompileShader(std::string_view source, VkShaderStageFlagBits shaderStage,
              std::filesystem::path const& path,
              gsl::span<std::string> macroDefinitions [[maybe_unused]],
              std::string const& entryPoint) {
  IRIS_LOG_ENTER();
  Expects(source.size() > 0);

  auto const lang = [&shaderStage]() {
    if ((shaderStage & VK_SHADER_STAGE_VERTEX_BIT)) {
      return EShLanguage::EShLangVertex;
    } else if ((shaderStage & VK_SHADER_STAGE_FRAGMENT_BIT)) {
      return EShLanguage::EShLangFragment;
    } else if ((shaderStage & VK_SHADER_STAGE_RAYGEN_BIT_NV)) {
      return EShLanguage::EShLangRayGenNV;
    } else if ((shaderStage & VK_SHADER_STAGE_ANY_HIT_BIT_NV)) {
      return EShLanguage::EShLangAnyHitNV;
    } else if ((shaderStage & VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV)) {
      return EShLanguage::EShLangClosestHitNV;
    } else if ((shaderStage & VK_SHADER_STAGE_INTERSECTION_BIT_NV)) {
      return EShLanguage::EShLangIntersectNV;
    } else if ((shaderStage & VK_SHADER_STAGE_MISS_BIT_NV)) {
      return EShLanguage::EShLangMissNV;
    } else if ((shaderStage & VK_SHADER_STAGE_CALLABLE_BIT_NV)) {
      return EShLanguage::EShLangCallableNV;
    } else if ((shaderStage & VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT)) {
      return EShLanguage::EShLangTessControl;
    } else if ((shaderStage & VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT)) {
      return EShLanguage::EShLangTessEvaluation;
    } else if ((shaderStage & VK_SHADER_STAGE_GEOMETRY_BIT)) {
      return EShLanguage::EShLangGeometry;
    } else {
      IRIS_LOG_CRITICAL("Unhandled shaderStage: {}", shaderStage);
      std::terminate();
    }
  }();

  char const* strings[] = {source.data()};
  int lengths[] = {static_cast<int>(source.size())};
  char const* names[] = {path.string().c_str()};

  std::string preamble;
  for (auto&& define : macroDefinitions) {
    preamble += define;
    preamble += "\n";
  }

  glslang::TShader shader(lang);
  shader.setStringsWithLengthsAndNames(strings, lengths, names, 1);
  shader.setPreamble(preamble.c_str());
  shader.setEntryPoint(entryPoint.c_str());
  shader.setEnvInput(glslang::EShSource::EShSourceGlsl, lang,
                     glslang::EShClient::EShClientVulkan, 101);
  shader.setEnvClient(glslang::EShClient::EShClientVulkan,
                      glslang::EShTargetClientVersion::EShTargetVulkan_1_1);
  shader.setEnvTarget(glslang::EShTargetLanguage::EShTargetSpv,
                      glslang::EShTargetLanguageVersion::EShTargetSpv_1_0);

  DirStackIncluder includer;
  includer.pushExternalLocalDirectory(kIRISContentDirectory);

  if (!shader.parse(&DefaultTBuiltInResource, 1, false,
                    EShMessages::EShMsgDefault, includer)) {
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
} // CompileShader

} // namespace iris

tl::expected<iris::Shader, std::system_error> iris::CompileShaderFromSource(
  std::string_view source, VkShaderStageFlagBits stage,
  gsl::span<std::string> macroDefinitions) noexcept {
  IRIS_LOG_ENTER();
  Expects(Renderer::sDevice != VK_NULL_HANDLE);
  Expects(source.size() > 0);

  Shader shader;
  shader.stage = stage;

  auto code =
    CompileShader(source, shader.stage, "<inline>", macroDefinitions, "main");
  if (!code) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(Error::kShaderCompileFailed, code.error()));
  }

  VkShaderModuleCreateInfo shaderModuleCI = {};
  shaderModuleCI.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  // codeSize is count of bytes, not count of words (which is what size() is)
  shaderModuleCI.codeSize = gsl::narrow_cast<std::uint32_t>(code->size()) * 4u;
  shaderModuleCI.pCode = code->data();

  if (auto result = vkCreateShaderModule(Renderer::sDevice, &shaderModuleCI,
                                         nullptr, &shader.module);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(make_error_code(result),
                                            "Cannot create shader module"));
  }

  Ensures(shader.module != VK_NULL_HANDLE);

  IRIS_LOG_LEAVE();
  return shader;
} // iris::CompileShaderFromSource

tl::expected<iris::Shader, std::system_error>
iris::LoadShaderFromFile(std::filesystem::path const& path,
                         VkShaderStageFlagBits stage,
                         gsl::span<std::string> macroDefinitions) noexcept {
  IRIS_LOG_ENTER();
  Expects(!path.empty());
  Expects(Renderer::sDevice != VK_NULL_HANDLE);

  auto bytes = iris::io::ReadFile(path);

  if (!bytes) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(bytes.error());
  }

  Shader shader;
  shader.stage = stage;

  auto code =
    CompileShader({reinterpret_cast<char const*>(bytes->data()), bytes->size()},
                  shader.stage, path, macroDefinitions, "main");
  if (!code) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(Error::kShaderCompileFailed, code.error()));
  }

  VkShaderModuleCreateInfo shaderModuleCI = {};
  shaderModuleCI.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  // codeSize is count of bytes, not count of words (which is what size() is)
  shaderModuleCI.codeSize = gsl::narrow_cast<std::uint32_t>(code->size()) * 4u;
  shaderModuleCI.pCode = code->data();

  if (auto result = vkCreateShaderModule(Renderer::sDevice, &shaderModuleCI,
                                         nullptr, &shader.module);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(make_error_code(result),
                                            "Cannot create shader module"));
  }

  Ensures(shader.module != VK_NULL_HANDLE);

  IRIS_LOG_LEAVE();
  return shader;
} // iris::LoadShaderFromFile

