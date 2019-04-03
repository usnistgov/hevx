#include "renderer_util.h"
#if PLATFORM_COMPILER_GCC
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#include "absl/container/inlined_vector.h"
#include "enumerate.h"
#include "error.h"
#include "io/read_file.h"
#include "iris/renderer.h"
#include "logging.h"
#include "vulkan_util.h"

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

tl::expected<VkCommandBuffer, std::system_error>
iris::Renderer::BeginOneTimeSubmit(VkCommandPool commandPool) noexcept {
  IRIS_LOG_ENTER();
  Expects(sDevice != VK_NULL_HANDLE);
  Expects(commandPool != VK_NULL_HANDLE);

  VkCommandBufferAllocateInfo commandBufferAI = {};
  commandBufferAI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  commandBufferAI.commandPool = commandPool;
  commandBufferAI.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  commandBufferAI.commandBufferCount = 1;

  VkCommandBuffer commandBuffer;
  if (auto result =
        vkAllocateCommandBuffers(sDevice, &commandBufferAI, &commandBuffer);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(make_error_code(result),
                                            "Cannot allocate command buffer"));
  }

  VkCommandBufferBeginInfo commandBufferBI = {};
  commandBufferBI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  commandBufferBI.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

  if (auto result = vkBeginCommandBuffer(commandBuffer, &commandBufferBI);
      result != VK_SUCCESS) {
    vkFreeCommandBuffers(sDevice, commandPool, 1, &commandBuffer);
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(make_error_code(result),
                                            "Cannot begin command buffer"));
  }

  IRIS_LOG_LEAVE();
  return commandBuffer;
} // iris::Renderer::BeginOneTimeSubmit

tl::expected<void, std::system_error>
iris::Renderer::EndOneTimeSubmit(VkCommandBuffer commandBuffer,
                                 VkCommandPool commandPool, VkQueue queue,
                                 VkFence fence) noexcept {
  IRIS_LOG_ENTER();
  Expects(sDevice != VK_NULL_HANDLE);
  Expects(commandBuffer != VK_NULL_HANDLE);
  Expects(commandPool != VK_NULL_HANDLE);
  Expects(queue != VK_NULL_HANDLE);
  Expects(fence != VK_NULL_HANDLE);

  VkSubmitInfo submitI = {};
  submitI.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitI.commandBufferCount = 1;
  submitI.pCommandBuffers = &commandBuffer;

  if (auto result = vkEndCommandBuffer(commandBuffer); result != VK_SUCCESS) {
    vkFreeCommandBuffers(sDevice, commandPool, 1, &commandBuffer);
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(make_error_code(result), "Cannot end command buffer"));
  }

  if (auto result = vkQueueSubmit(queue, 1, &submitI, fence);
      result != VK_SUCCESS) {
    vkFreeCommandBuffers(sDevice, commandPool, 1, &commandBuffer);
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(make_error_code(result),
                                            "Cannot submit command buffer"));
  }

  if (auto result = vkWaitForFences(sDevice, 1, &fence, VK_TRUE, UINT64_MAX);
      result != VK_SUCCESS) {
    vkFreeCommandBuffers(sDevice, commandPool, 1, &commandBuffer);
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(
      make_error_code(result), "Cannot wait on one-time submit fence"));
  }

  if (auto result = vkResetFences(sDevice, 1, &fence); result != VK_SUCCESS) {
    vkFreeCommandBuffers(sDevice, commandPool, 1, &commandBuffer);
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(
      make_error_code(result), "Cannot reset one-time submit fence"));
  }

  vkFreeCommandBuffers(sDevice, commandPool, 1, &commandBuffer);
  IRIS_LOG_LEAVE();
  return {};
} // iris::Renderer::EndOneTimeSubmit

namespace iris::Renderer {

using namespace std::string_literals;

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

class DirStackIncluder : public glslang::TShader::Includer {
public:
  DirStackIncluder() noexcept = default;

  virtual IncludeResult* includeLocal(char const* headerName,
                                      char const* includerName,
                                      std::size_t inclusionDepth) override {
    return readLocalPath(headerName, includerName, inclusionDepth);
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
    numExternalLocalDirs_ = dirStack_.size();
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
        return newIncludeResult(path, ifs, ifs.tellg());
      }
    }

    return nullptr;
  }

  virtual IncludeResult* readSystemPath(char const*) const {
    GetLogger()->error("including system headers not implemented");
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
              filesystem::path const& path,
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

} // namespace iris::Renderer

tl::expected<VkShaderModule, std::system_error>
iris::Renderer::CompileShaderFromSource(std::string_view source,
                                        VkShaderStageFlagBits stage) noexcept {
  IRIS_LOG_ENTER();
  Expects(sDevice != VK_NULL_HANDLE);
  Expects(source.size() > 0);

  VkShaderModule module = VK_NULL_HANDLE;

  auto code = CompileShader(source, stage, "<inline>", {}, "main");
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

  if (auto result =
        vkCreateShaderModule(sDevice, &shaderModuleCI, nullptr, &module);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(make_error_code(result),
                                            "Cannot create shader module"));
  }

  Ensures(module != VK_NULL_HANDLE);

  IRIS_LOG_LEAVE();
  return module;
} // iris::Renderer::CompileShaderFromSource

tl::expected<VkShaderModule, std::system_error>
iris::Renderer::LoadShaderFromFile(filesystem::path const& path,
                                   VkShaderStageFlagBits stage) noexcept {
  IRIS_LOG_ENTER();
  Expects(sDevice != VK_NULL_HANDLE);
  Expects(!path.empty());

  auto bytes = iris::io::ReadFile(path);

  if (!bytes) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(bytes.error());
  }

  auto sm = iris::Renderer::CompileShaderFromSource(
    {reinterpret_cast<char const*>(bytes->data()), bytes->size()}, stage);
  if (!sm) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(sm.error());
  }

  IRIS_LOG_LEAVE();
  return *sm;
} // iris::Renderer::LoadShaderFromFile

tl::expected<iris::Renderer::AccelerationStructure, std::system_error>
iris::Renderer::CreateAccelerationStructure(
  VkAccelerationStructureInfoNV const& accelerationStructureInfo,
  VkDeviceSize compactedSize) noexcept {
  IRIS_LOG_ENTER();
  Expects(sDevice != VK_NULL_HANDLE);
  Expects(sAllocator != VK_NULL_HANDLE);

  VkAccelerationStructureCreateInfoNV accelerationStructureCI = {};
  accelerationStructureCI.sType =
    VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_NV;
  accelerationStructureCI.compactedSize = compactedSize;
  accelerationStructureCI.info = accelerationStructureInfo;

  AccelerationStructure structure;
  if (auto result = vkCreateAccelerationStructureNV(
        sDevice, &accelerationStructureCI, nullptr, &structure.structure);
      result != VK_SUCCESS) {
    return tl::unexpected(std::system_error(
      make_error_code(result), "Cannot create acceleration structure"));
  }

  VkMemoryRequirements2KHR memoryRequirements = {};
  memoryRequirements.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2_KHR;

  VkAccelerationStructureMemoryRequirementsInfoNV memoryRequirementsInfo = {};
  memoryRequirementsInfo.sType =
    VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_NV;
  memoryRequirementsInfo.accelerationStructure = structure.structure;
  memoryRequirementsInfo.type =
    VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_OBJECT_NV;
  vkGetAccelerationStructureMemoryRequirementsNV(
    sDevice, &memoryRequirementsInfo, &memoryRequirements);

  VmaAllocationCreateInfo allocationCI = {};
  allocationCI.flags = VMA_MEMORY_USAGE_GPU_ONLY;
  allocationCI.memoryTypeBits =
    memoryRequirements.memoryRequirements.memoryTypeBits;

  if (auto result =
        vmaAllocateMemory(sAllocator, &memoryRequirements.memoryRequirements,
                          &allocationCI, &structure.allocation, nullptr);
      result != VK_SUCCESS) {
    vkDestroyAccelerationStructureNV(sDevice, structure.structure, nullptr);
    return tl::unexpected(std::system_error(iris::make_error_code(result),
                                            "Cannot allocate memory"));
  }

  VmaAllocationInfo allocationInfo;
  vmaGetAllocationInfo(sAllocator, structure.allocation, &allocationInfo);

  VkBindAccelerationStructureMemoryInfoNV bindAccelerationStructureMemoryInfo =
    {};
  bindAccelerationStructureMemoryInfo.sType =
    VK_STRUCTURE_TYPE_BIND_ACCELERATION_STRUCTURE_MEMORY_INFO_NV;
  bindAccelerationStructureMemoryInfo.accelerationStructure =
    structure.structure;
  bindAccelerationStructureMemoryInfo.memory = allocationInfo.deviceMemory;
  bindAccelerationStructureMemoryInfo.memoryOffset = 0;

  if (auto result = vkBindAccelerationStructureMemoryNV(
        sDevice, 1, &bindAccelerationStructureMemoryInfo);
      result != VK_SUCCESS) {
    vmaFreeMemory(sAllocator, structure.allocation);
    vkDestroyAccelerationStructureNV(sDevice, structure.structure, nullptr);
    return tl::unexpected(std::system_error(
      make_error_code(result), "Cannot bind memory to acceleration structure"));
  }

  Ensures(structure.structure != VK_NULL_HANDLE);
  Ensures(structure.allocation != VK_NULL_HANDLE);

  IRIS_LOG_LEAVE();
  return structure;
} // iris::Renderer::CreateAccelerationStructure

tl::expected<iris::Renderer::Pipeline, std::system_error>
iris::Renderer::CreateRasterizationPipeline(
  gsl::span<const Shader> shaders,
  gsl::span<const VkVertexInputBindingDescription>
    vertexInputBindingDescriptions,
  gsl::span<const VkVertexInputAttributeDescription>
    vertexInputAttributeDescriptions,
  VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI,
  VkPipelineViewportStateCreateInfo viewportStateCI,
  VkPipelineRasterizationStateCreateInfo rasterizationStateCI,
  VkPipelineMultisampleStateCreateInfo multisampleStateCI,
  VkPipelineDepthStencilStateCreateInfo depthStencilStateCI,
  gsl::span<const VkPipelineColorBlendAttachmentState>
    colorBlendAttachmentStates,
  gsl::span<const VkDynamicState> dynamicStates,
  std::uint32_t renderPassSubpass,
  gsl::span<const VkDescriptorSetLayout> descriptorSetLayouts) noexcept {
  IRIS_LOG_ENTER();
  Expects(sDevice != VK_NULL_HANDLE);
  Expects(sRenderPass != VK_NULL_HANDLE);
  Expects(sGlobalDescriptorSetLayout != VK_NULL_HANDLE);

  Pipeline pipeline;

  absl::InlinedVector<VkDescriptorSetLayout, 8> allDescriptorSetLayouts;
  allDescriptorSetLayouts.push_back(sGlobalDescriptorSetLayout);
  std::copy_n(std::begin(descriptorSetLayouts), std::size(descriptorSetLayouts),
              std::back_inserter(allDescriptorSetLayouts));

  VkPushConstantRange pushConstantRange = {};
  pushConstantRange.stageFlags =
    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  pushConstantRange.offset = 0;
  pushConstantRange.size = sizeof(PushConstants);

  VkPipelineLayoutCreateInfo pipelineLayoutCI = {};
  pipelineLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutCI.setLayoutCount =
    gsl::narrow_cast<std::uint32_t>(allDescriptorSetLayouts.size());
  pipelineLayoutCI.pSetLayouts = allDescriptorSetLayouts.data();
  pipelineLayoutCI.pushConstantRangeCount = 1;
  pipelineLayoutCI.pPushConstantRanges = &pushConstantRange;

  if (auto result = vkCreatePipelineLayout(sDevice, &pipelineLayoutCI, nullptr,
                                           &pipeline.layout);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(make_error_code(result),
                                            "Cannot create pipeline layout"));
  }

  absl::FixedArray<VkPipelineShaderStageCreateInfo> shaderStageCIs(
    shaders.size());
  std::transform(shaders.begin(), shaders.end(), shaderStageCIs.begin(),
                 [](Shader const& shader) {
                   return VkPipelineShaderStageCreateInfo{
                     VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                     nullptr,
                     0,
                     shader.stage,
                     shader.handle,
                     "main",
                     nullptr};
                 });

  VkPipelineVertexInputStateCreateInfo vertexInputStateCI = {};
  vertexInputStateCI.sType =
    VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertexInputStateCI.vertexBindingDescriptionCount =
    gsl::narrow_cast<std::uint32_t>(vertexInputBindingDescriptions.size());
  vertexInputStateCI.pVertexBindingDescriptions =
    vertexInputBindingDescriptions.data();
  vertexInputStateCI.vertexAttributeDescriptionCount =
    gsl::narrow_cast<std::uint32_t>(vertexInputAttributeDescriptions.size());
  vertexInputStateCI.pVertexAttributeDescriptions =
    vertexInputAttributeDescriptions.data();

  VkPipelineColorBlendStateCreateInfo colorBlendStateCI = {};
  colorBlendStateCI.sType =
    VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlendStateCI.attachmentCount =
    gsl::narrow_cast<std::uint32_t>(colorBlendAttachmentStates.size());
  colorBlendStateCI.pAttachments = colorBlendAttachmentStates.data();

  VkPipelineDynamicStateCreateInfo dynamicStateCI = {};
  dynamicStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamicStateCI.dynamicStateCount =
    gsl::narrow_cast<uint32_t>(dynamicStates.size());
  dynamicStateCI.pDynamicStates = dynamicStates.data();

  VkGraphicsPipelineCreateInfo graphicsPipelineCI = {};
  graphicsPipelineCI.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  graphicsPipelineCI.stageCount =
    gsl::narrow_cast<std::uint32_t>(shaderStageCIs.size());
  graphicsPipelineCI.pStages = shaderStageCIs.data();
  graphicsPipelineCI.pVertexInputState = &vertexInputStateCI;
  graphicsPipelineCI.pInputAssemblyState = &inputAssemblyStateCI;
  graphicsPipelineCI.pViewportState = &viewportStateCI;
  graphicsPipelineCI.pRasterizationState = &rasterizationStateCI;
  graphicsPipelineCI.pMultisampleState = &multisampleStateCI;
  graphicsPipelineCI.pDepthStencilState = &depthStencilStateCI;
  graphicsPipelineCI.pColorBlendState = &colorBlendStateCI;
  graphicsPipelineCI.pDynamicState = &dynamicStateCI;
  graphicsPipelineCI.layout = pipeline.layout;
  graphicsPipelineCI.renderPass = sRenderPass;
  graphicsPipelineCI.subpass = renderPassSubpass;

  if (auto result = vkCreateGraphicsPipelines(sDevice, VK_NULL_HANDLE, 1,
                                              &graphicsPipelineCI, nullptr,
                                              &pipeline.pipeline);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    vkDestroyPipelineLayout(sDevice, pipeline.layout, nullptr);
    return tl::unexpected(std::system_error(make_error_code(result),
                                            "Cannot create graphics pipeline"));
  }

  Ensures(pipeline.layout != VK_NULL_HANDLE);
  Ensures(pipeline.pipeline != VK_NULL_HANDLE);

  IRIS_LOG_LEAVE();
  return pipeline;
} // iris::Renderer::CreateRasterizationPipeline

tl::expected<iris::Renderer::Pipeline, std::system_error>
iris::Renderer::CreateRayTracingPipeline(
  gsl::span<const Shader> shaders, gsl::span<const ShaderGroup> groups,
  gsl::span<const VkDescriptorSetLayout> descriptorSetLayouts,
  std::uint32_t maxRecursionDepth) noexcept {
  IRIS_LOG_ENTER();
  Expects(sDevice != VK_NULL_HANDLE);
  Expects(sGlobalDescriptorSetLayout != VK_NULL_HANDLE);

  Pipeline pipeline;

  absl::InlinedVector<VkDescriptorSetLayout, 8> allDescriptorSetLayouts;
  allDescriptorSetLayouts.push_back(sGlobalDescriptorSetLayout);
  std::copy_n(std::begin(descriptorSetLayouts), std::size(descriptorSetLayouts),
              std::back_inserter(allDescriptorSetLayouts));

  VkPushConstantRange pushConstantRange = {};
  pushConstantRange.stageFlags =
    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  pushConstantRange.offset = 0;
  pushConstantRange.size = sizeof(PushConstants);

  VkPipelineLayoutCreateInfo pipelineLayoutCI = {};
  pipelineLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutCI.setLayoutCount =
    gsl::narrow_cast<std::uint32_t>(allDescriptorSetLayouts.size());
  pipelineLayoutCI.pSetLayouts = allDescriptorSetLayouts.data();
  pipelineLayoutCI.pushConstantRangeCount = 1;
  pipelineLayoutCI.pPushConstantRanges = &pushConstantRange;

  if (auto result = vkCreatePipelineLayout(sDevice, &pipelineLayoutCI, nullptr,
                                           &pipeline.layout);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(make_error_code(result),
                                            "Cannot create pipeline layout"));
  }

  absl::FixedArray<VkPipelineShaderStageCreateInfo> shaderStageCIs(
    shaders.size());
  std::transform(shaders.begin(), shaders.end(), shaderStageCIs.begin(),
                 [](Shader const& shader) {
                   return VkPipelineShaderStageCreateInfo{
                     VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                     nullptr,
                     0,
                     shader.stage,
                     shader.handle,
                     "main",
                     nullptr};
                 });

  absl::FixedArray<VkRayTracingShaderGroupCreateInfoNV> shaderGroupCIs(
    groups.size());
  std::transform(groups.begin(), groups.end(), shaderGroupCIs.begin(),
                 [](ShaderGroup const& group) {
                   return VkRayTracingShaderGroupCreateInfoNV{
                     VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_NV,
                     nullptr,
                     group.type,
                     group.generalShaderIndex,
                     group.closestHitShaderIndex,
                     group.anyHitShaderIndex,
                     group.intersectionShaderIndex};
                 });

  VkRayTracingPipelineCreateInfoNV pipelineCI = {};
  pipelineCI.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_NV;
  pipelineCI.stageCount = shaderStageCIs.size();
  pipelineCI.pStages = shaderStageCIs.data();
  pipelineCI.groupCount = shaderGroupCIs.size();
  pipelineCI.pGroups = shaderGroupCIs.data();
  pipelineCI.maxRecursionDepth = maxRecursionDepth;
  pipelineCI.layout = pipeline.layout;

  if (auto result = vkCreateRayTracingPipelinesNV(
        iris::Renderer::sDevice, VK_NULL_HANDLE, 1, &pipelineCI, nullptr,
        &pipeline.pipeline);
      result != VK_SUCCESS) {
    return tl::unexpected(std::system_error(iris::make_error_code(result),
                                            "Cannot create pipeline"));
  }

  Ensures(pipeline.layout != VK_NULL_HANDLE);
  Ensures(pipeline.pipeline != VK_NULL_HANDLE);

  IRIS_LOG_LEAVE();
  return pipeline;
} // iris::Renderer::CreateRayTracingPipeline

