#include "iris/config.h"

#include "absl/debugging/failure_signal_handler.h"
#include "absl/debugging/symbolize.h"
#include "fmt/format.h"
#include "glm/mat4x4.hpp"
#include "iris/io/read_file.h"
#include "iris/renderer.h"
#include "iris/renderer_util.h"
#include "iris/protos.h"
#if PLATFORM_COMPILER_MSVC
#pragma warning(push)
#pragma warning(disable : 4127)
#endif
#include "spdlog/logger.h"
#include "spdlog/sinks/ansicolor_sink.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#if PLATFORM_COMPILER_MSVC
#pragma warning(pop)
#endif
#include "flags.h"
#include <cstdlib>
#include <exception>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

static iris::Renderer::CommandQueue sCommandQueue;

struct Matrices {
  glm::mat4 model;
  glm::mat4 view;
  glm::mat4 proj;
  glm::mat4 modelIT;
  glm::mat4 viewInverse;
  glm::mat4 projInverse;
} gMatrices;

tl::expected<std::tuple<VkDescriptorPool, VkDescriptorSetLayout, VkDescriptorSet>,
             std::system_error>
CreateDescriptor() noexcept {
  absl::FixedArray<VkDescriptorPoolSize, 3> poolSizes{{
    {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 32},
    {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV, 32},
    {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 32},
  }};

  VkDescriptorPoolCreateInfo poolCI = {};
  poolCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  poolCI.maxSets = 32;
  poolCI.poolSizeCount = gsl::narrow_cast<std::uint32_t>(poolSizes.size());
  poolCI.pPoolSizes = poolSizes.data();

  VkDescriptorPool pool;
  if (auto result = vkCreateDescriptorPool(iris::Renderer::sDevice, &poolCI,
                                           nullptr, &pool);
      result != VK_SUCCESS) {
    return tl::unexpected(
      std::system_error(iris::Renderer::make_error_code(result),
                        "Cannot create descriptor pool"));
  }

  absl::FixedArray<VkDescriptorSetLayoutBinding, 3> bindings{{
    {0, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV, 1,
     VK_SHADER_STAGE_RAYGEN_BIT_NV, nullptr},
    {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_RAYGEN_BIT_NV,
     nullptr},
    {2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
     VK_SHADER_STAGE_RAYGEN_BIT_NV | VK_SHADER_STAGE_INTERSECTION_BIT_NV,
     nullptr},
  }};

  VkDescriptorSetLayoutCreateInfo layoutCI = {};
  layoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layoutCI.bindingCount = gsl::narrow_cast<std::uint32_t>(bindings.size());
  layoutCI.pBindings = bindings.data();

  VkDescriptorSetLayout layout = {};
  if (auto result = vkCreateDescriptorSetLayout(iris::Renderer::sDevice,
                                                &layoutCI, nullptr, &layout);
      result != VK_SUCCESS) {
    return tl::unexpected(
      std::system_error(iris::Renderer::make_error_code(result),
                        "Cannot create descriptor set layout"));
  }

  VkDescriptorSetAllocateInfo setAI = {};
  setAI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  setAI.descriptorPool = pool;
  setAI.descriptorSetCount = 1;
  setAI.pSetLayouts = &layout;

  VkDescriptorSet set;
  if (auto result =
        vkAllocateDescriptorSets(iris::Renderer::sDevice, &setAI, &set);
      result != VK_SUCCESS) {
    return tl::unexpected(
      std::system_error(iris::Renderer::make_error_code(result),
                        "Cannot allocate descriptor set"));
  }

  return std::make_tuple(pool, layout, set);
}

tl::expected<VkShaderModule, std::system_error>
LoadShaderFromFile(filesystem::path const& path, VkShaderStageFlagBits stage) noexcept {
  std::string rayGenSource;
  if (auto bytes = iris::io::ReadFile(path)) {
    if (auto module = iris::Renderer::CompileShaderFromSource(
          iris::Renderer::sDevice,
          {reinterpret_cast<char const*>(bytes->data()), bytes->size()},
          stage)) {
      return *module;
    } else {
      return tl::unexpected(module.error());
    }
    } else {
      return tl::unexpected(bytes.error());
    }
}

tl::expected<std::tuple<VkPipelineLayout, VkPipeline>, std::system_error>
CreatePipeline(VkDescriptorSetLayout setLayout) noexcept {
  using namespace std::string_literals;

  VkPipelineLayoutCreateInfo layoutCI = {};
  layoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  layoutCI.setLayoutCount = 1;
  layoutCI.pSetLayouts = &setLayout;

  VkPipelineLayout layout;
  if (auto result = vkCreatePipelineLayout(iris::Renderer::sDevice, &layoutCI,
                                           nullptr, &layout);
      result != VK_SUCCESS) {
    return tl::unexpected(
      std::system_error(iris::Renderer::make_error_code(result),
                        "Cannot create pipeline layout"));
  }

  VkShaderModule rayGenSM;
  if (auto sm = LoadShaderFromFile(iris::kIRISContentDirectory +
                                     "/assets/shaders/raytracing/raygen.glsl"s,
                                   VK_SHADER_STAGE_RAYGEN_BIT_NV)) {
    rayGenSM = *sm;
  } else {
    return tl::unexpected(std::system_error(
      sm.error().code(), "Cannot load raygen.glsl: "s + sm.error().what()));
  }

  VkShaderModule missSM;
  if (auto sm = LoadShaderFromFile(iris::kIRISContentDirectory +
                                     "/assets/shaders/raytracing/miss.glsl"s,
                                   VK_SHADER_STAGE_MISS_BIT_NV)) {
    missSM = *sm;
  } else {
    return tl::unexpected(std::system_error(
      sm.error().code(), "Cannot load miss.glsl: "s + sm.error().what()));
  }

  VkShaderModule closestHitSM;
  if (auto sm =
        LoadShaderFromFile(iris::kIRISContentDirectory +
                             "/assets/shaders/raytracing/closest_hit.glsl"s,
                           VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV)) {
    closestHitSM = *sm;
  } else {
    return tl::unexpected(
      std::system_error(sm.error().code(),
                        "Cannot load closest_hit.glsl: "s + sm.error().what()));
  }

  VkShaderModule sphereIntersectSM;
  if (auto sm = LoadShaderFromFile(
        iris::kIRISContentDirectory +
          "/assets/shaders/raytracing/sphere_intersect.glsl"s,
        VK_SHADER_STAGE_INTERSECTION_BIT_NV)) {
    sphereIntersectSM = *sm;
  } else {
    return tl::unexpected(std::system_error(
      sm.error().code(),
      "Cannot load sphere_intersect.glsl: "s + sm.error().what()));
  }

  absl::FixedArray<VkPipelineShaderStageCreateInfo, 4> shaderStages{
    VkPipelineShaderStageCreateInfo{
      VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
      VK_SHADER_STAGE_RAYGEN_BIT_NV, rayGenSM, "main", nullptr},
    VkPipelineShaderStageCreateInfo{
      VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
      VK_SHADER_STAGE_MISS_BIT_NV, missSM, "main", nullptr},
    VkPipelineShaderStageCreateInfo{
      VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
      VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV, closestHitSM, "main", nullptr},
    VkPipelineShaderStageCreateInfo{
      VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
      VK_SHADER_STAGE_INTERSECTION_BIT_NV, sphereIntersectSM, "main", nullptr},
  };

  absl::FixedArray<VkRayTracingShaderGroupCreateInfoNV> groups{
    VkRayTracingShaderGroupCreateInfoNV{
      VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_NV, nullptr,
      VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_NV, 0, 0, 0, 0},
    VkRayTracingShaderGroupCreateInfoNV{
      VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_NV, nullptr,
      VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_NV, 1, 0, 0, 0},
    VkRayTracingShaderGroupCreateInfoNV{
      VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_NV, nullptr,
      VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_NV, 0, 2, 0, 3}};

  VkRayTracingPipelineCreateInfoNV pipelineCI = {};
  pipelineCI.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_NV;
  pipelineCI.stageCount = shaderStages.size();
  pipelineCI.pStages = shaderStages.data();
  pipelineCI.groupCount = gsl::narrow_cast<std::uint32_t>(groups.size());
  pipelineCI.pGroups = groups.data();
  pipelineCI.maxRecursionDepth = 2;
  pipelineCI.layout = layout;

  VkPipeline pipeline;
  if (auto result =
        vkCreateRayTracingPipelinesNV(iris::Renderer::sDevice, VK_NULL_HANDLE,
                                      1, &pipelineCI, nullptr, &pipeline);
      result != VK_SUCCESS) {
    return tl::unexpected(std::system_error(
      iris::Renderer::make_error_code(result), "Cannot create pipeline"));
  }

  return std::make_tuple(layout, pipeline);
} // CreatePipeline

tl::expected<std::tuple<VkAccelerationStructureNV, VkAccelerationStructureNV>,
             std::system_error>
CreateAccelerationStructures(spdlog::logger& logger) noexcept {
  using namespace std::string_literals;

  /////
  //
  // Bottom Level AS
  //
  /////

  VkBuffer aabbBuffer{VK_NULL_HANDLE};
  VmaAllocation aabbBufferAllocation{VK_NULL_HANDLE};
  VkDeviceSize aabbBufferSize{0};

  if (auto bas = iris::Renderer::CreateOrResizeBuffer(
        iris::Renderer::sAllocator, aabbBuffer, aabbBufferAllocation,
        aabbBufferSize, 24, VK_BUFFER_USAGE_RAY_TRACING_BIT_NV,
        VMA_MEMORY_USAGE_CPU_TO_GPU)) {
    std::tie(aabbBuffer, aabbBufferAllocation, aabbBufferSize) = *bas;
  } else {
    return tl::unexpected(std::system_error(
      bas.error().code(), "Cannot create aabb buffer: "s + bas.error().what()));
  }

  void* aabbPointer;
  if (auto result = vmaMapMemory(iris::Renderer::sAllocator,
                                 aabbBufferAllocation, &aabbPointer);
      result != VK_SUCCESS) {
    return tl::unexpected(std::system_error(
      iris::Renderer::make_error_code(result), "Cannot map aabb buffer: "));
  }

  float* aabbData = reinterpret_cast<float*>(aabbPointer);
  aabbData[0] = aabbData[1] = aabbData[2] = -1.f;
  aabbData[3] = aabbData[4] = aabbData[5] =  1.f;

  vmaUnmapMemory(iris::Renderer::sAllocator, aabbBufferAllocation);

  VkGeometryNV geometry = {};
  geometry.sType = VK_STRUCTURE_TYPE_GEOMETRY_NV;
  geometry.flags = VK_GEOMETRY_OPAQUE_BIT_NV;
  geometry.geometryType = VK_GEOMETRY_TYPE_AABBS_NV;
  geometry.flags = VK_GEOMETRY_OPAQUE_BIT_NV;
  geometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_GEOMETRY_TRIANGLES_NV;
  geometry.geometry.triangles.pNext = nullptr;
  geometry.geometry.aabbs.sType = VK_STRUCTURE_TYPE_GEOMETRY_AABB_NV;
  geometry.geometry.aabbs.pNext = nullptr;
  geometry.geometry.aabbs.aabbData = aabbBuffer;
  geometry.geometry.aabbs.numAABBs = 1;
  geometry.geometry.aabbs.stride = 24; // 6 floats == 6 x 4 bytes == 24 bytes
  geometry.geometry.aabbs.offset = 0;

  VkAccelerationStructureCreateInfoNV accelerationStructureCI = {};
  accelerationStructureCI.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_NV;
  accelerationStructureCI.compactedSize = 0;
  accelerationStructureCI.info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_INFO_NV;
  accelerationStructureCI.info.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_NV;
  accelerationStructureCI.info.flags = 0;
  accelerationStructureCI.info.instanceCount = 0;
  accelerationStructureCI.info.geometryCount = 1;
  accelerationStructureCI.info.pGeometries = &geometry;

  VkAccelerationStructureNV bottomLevelAS{VK_NULL_HANDLE};
  VmaAllocation bottomLevelASAllocation{VK_NULL_HANDLE};
  if (auto as = iris::Renderer::CreateAccelerationStructure(
        iris::Renderer::sDevice, iris::Renderer::sAllocator,
        &accelerationStructureCI)) {
    std::tie(bottomLevelAS, bottomLevelASAllocation) = *as;
  } else {
    return tl::unexpected(
      std::system_error(as.error().code(), "Cannot create bottom level AS: "s +
                                             as.error().what()));
  }

  VkMemoryRequirements2KHR memoryRequirements = {};
  memoryRequirements.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2_KHR;

  VkAccelerationStructureMemoryRequirementsInfoNV memoryRequirementsInfo = {};
  memoryRequirementsInfo.sType =
    VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_NV;
  memoryRequirementsInfo.accelerationStructure = bottomLevelAS;
  memoryRequirementsInfo.type =
    VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_BUILD_SCRATCH_NV;
  vkGetAccelerationStructureMemoryRequirementsNV(
    iris::Renderer::sDevice, &memoryRequirementsInfo, &memoryRequirements);

  VkBuffer scratchBuffer{VK_NULL_HANDLE};
  VmaAllocation scratchAllocation{VK_NULL_HANDLE};
  VkDeviceSize scratchBufferSize = 0;

  logger.info("Creating scratch buffer for bottomLevelAS sized: {}",
              memoryRequirements.memoryRequirements.size);
  if (auto bas = iris::Renderer::CreateOrResizeBuffer(
        iris::Renderer::sAllocator, scratchBuffer, scratchAllocation,
        scratchBufferSize, memoryRequirements.memoryRequirements.size,
        VK_BUFFER_USAGE_RAY_TRACING_BIT_NV, VMA_MEMORY_USAGE_GPU_ONLY)) {
    std::tie(scratchBuffer, scratchAllocation, scratchBufferSize) = *bas;
  } else {
    return tl::unexpected(
      std::system_error(bas.error().code(), "Cannot allocate build memory: "s +
                                              bas.error().what()));
  }

  VkCommandBuffer commandBuffer;
  if (auto cb = iris::Renderer::BeginOneTimeSubmit(iris::Renderer::sDevice,
                                                   sCommandQueue.commandPool)) {
    commandBuffer = *cb;
  } else {
    return tl::unexpected(std::system_error(cb.error()));
  }

  logger.info("vkCmdBuildAccelerationStructureNV bottomLevelAS");
  vkCmdBuildAccelerationStructureNV(
    commandBuffer, &accelerationStructureCI.info,
    VK_NULL_HANDLE /* instanceData */, 0 /* instanceOffset */,
    VK_FALSE /* update */, bottomLevelAS /* dst */, VK_NULL_HANDLE /* src */,
    scratchBuffer, 0 /* scratchOffset */);

  logger.info("EndOneTimeSubmit bottomLevelAS");
  if (auto result = iris::Renderer::EndOneTimeSubmit(
        commandBuffer, iris::Renderer::sDevice, sCommandQueue.commandPool,
        sCommandQueue.queue, sCommandQueue.submitFence);
      !result) {
    return tl::unexpected(std::system_error(
      result.error().code(),
      "Cannot build acceleration struture: "s + result.error().what()));
  }

  /////
  //
  // Top Level AS
  //
  /////

  accelerationStructureCI.info.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_NV;
  accelerationStructureCI.info.instanceCount = 1;
  accelerationStructureCI.info.geometryCount = 0;
  accelerationStructureCI.info.pGeometries = nullptr;

  VkAccelerationStructureNV topLevelAS{VK_NULL_HANDLE};
  VmaAllocation topLevelASAllocation{VK_NULL_HANDLE};

  if (auto as = iris::Renderer::CreateAccelerationStructure(
        iris::Renderer::sDevice, iris::Renderer::sAllocator,
        &accelerationStructureCI)) {
    std::tie(topLevelAS, topLevelASAllocation) = *as;
  } else {
    return tl::unexpected(
      std::system_error(as.error().code(), "Cannot create top level AS: "s +
                                             as.error().what()));
  }

  VkBuffer instanceBuffer{VK_NULL_HANDLE};
  VmaAllocation instanceAllocation{VK_NULL_HANDLE};
  VkDeviceSize instanceBufferSize = 0;

  if (auto bas = iris::Renderer::CreateOrResizeBuffer(
        iris::Renderer::sAllocator, instanceBuffer, instanceAllocation,
        instanceBufferSize, 64, VK_BUFFER_USAGE_RAY_TRACING_BIT_NV,
        VMA_MEMORY_USAGE_CPU_TO_GPU)) {
    std::tie(instanceBuffer, instanceAllocation, instanceBufferSize) = *bas;
  } else {
    return tl::unexpected(std::system_error(
      bas.error().code(),
      "Cannot allocate instance buffer: "s + bas.error().what()));
  }
  logger.info("Created instance buffer for topLevelAS sized: {}",
              instanceBufferSize);

  absl::FixedArray<std::byte> bottomLevelASHandle(8);
  if (auto result = vkGetAccelerationStructureHandleNV(
        iris::Renderer::sDevice, bottomLevelAS, bottomLevelASHandle.size(),
        bottomLevelASHandle.data());
      result != VK_SUCCESS) {
    return tl::unexpected(
      std::system_error(iris::Renderer::make_error_code(result),
                        "Cannot get bottom level AS handle"));
  }

  struct VkGeometryInstanceNV {
    float transform[12];
    uint32_t instanceCustomIndex : 24;
    uint32_t mask : 8;
    uint32_t instanceOffset : 24;
    uint32_t flags : 8;
    uint64_t accelerationStructureHandle;
  };

  if (auto p = iris::Renderer::MapMemory<VkGeometryInstanceNV*>(
        iris::Renderer::sAllocator, instanceAllocation)) {
    for (int i = 0; i < 12; ++i) (*p)->transform[i] = 0.f;
    (*p)->instanceCustomIndex = 0;
    (*p)->mask = 0xF;
    (*p)->instanceOffset = 0;
    (*p)->flags = 0;
    (*p)->accelerationStructureHandle =
      *reinterpret_cast<uint64_t*>(bottomLevelASHandle.data());
  } else {
    return tl::unexpected(std::system_error(
      p.error().code(), "Cannot map instance buffer: "s + p.error().what()));
  }

  vmaUnmapMemory(iris::Renderer::sAllocator, instanceAllocation);

  memoryRequirementsInfo.accelerationStructure = topLevelAS;
  memoryRequirementsInfo.type =
    VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_BUILD_SCRATCH_NV;
  vkGetAccelerationStructureMemoryRequirementsNV(
    iris::Renderer::sDevice, &memoryRequirementsInfo, &memoryRequirements);

  logger.info("Creating scratch buffer for topLevelAS sized: {}",
              memoryRequirements.memoryRequirements.size);
  if (auto bas = iris::Renderer::CreateOrResizeBuffer(
        iris::Renderer::sAllocator, scratchBuffer, scratchAllocation,
        scratchBufferSize, memoryRequirements.memoryRequirements.size,
        VK_BUFFER_USAGE_RAY_TRACING_BIT_NV, VMA_MEMORY_USAGE_GPU_ONLY)) {
    std::tie(scratchBuffer, scratchAllocation, scratchBufferSize) = *bas;
  } else {
    return tl::unexpected(
      std::system_error(bas.error().code(), "Cannot allocate build memory: "s +
                                              bas.error().what()));
  }

  if (auto cb = iris::Renderer::BeginOneTimeSubmit(iris::Renderer::sDevice,
                                                   sCommandQueue.commandPool)) {
    commandBuffer = *cb;
  } else {
    return tl::unexpected(std::system_error(cb.error()));
  }

  logger.info("vkCmdBuildAccelerationStructureNV topLevelAS");
  vkCmdBuildAccelerationStructureNV(
    commandBuffer, &accelerationStructureCI.info,
    instanceBuffer /* instanceData */, 0 /* instanceOffset */,
    VK_FALSE /* update */, topLevelAS /* dst */, VK_NULL_HANDLE /* dst */,
    scratchBuffer, 0 /* scratchOffset */);

  if (auto result = iris::Renderer::EndOneTimeSubmit(
        commandBuffer, iris::Renderer::sDevice, sCommandQueue.commandPool,
        sCommandQueue.queue, sCommandQueue.submitFence);
      !result) {
    return tl::unexpected(std::system_error(
      result.error().code(),
      "Cannot build acceleration struture: "s + result.error().what()));
  }

  return std::make_tuple(bottomLevelAS, topLevelAS);
}

#if PLATFORM_WINDOWS
extern "C" {
_declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
}

#include <Windows.h>
#include <shellapi.h>

int CALLBACK WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
  // Oh my goodness
  char* cmdLine = ::GetCommandLineA();
  int argc = 1;
  char* argv[128]; // 128 command line argument max
  argv[0] = cmdLine;

  for (char* p = cmdLine; *p; ++p) {
    if (*p == ' ') {
      *p++ = '\0';
      if (*(p + 1)) argv[argc++] = p;
    }
  }

#else

int main(int argc, char** argv) {

#endif

  absl::InitializeSymbolizer(argv[0]);
  absl::InstallFailureSignalHandler({});

  flags::args const args(argc, argv);
  auto const& files = args.positional();

  auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
    "iris-raytracer.log", true);
  file_sink->set_level(spdlog::level::trace);

  auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
  console_sink->set_level(spdlog::level::trace);

  spdlog::logger logger("iris-viewer", {console_sink, file_sink});
  logger.set_level(spdlog::level::trace);

  logger.info("Logging initialized");

  if (auto result = iris::Renderer::Initialize(
        "iris-viewer",
        iris::Renderer::Options::kReportDebugMessages |
          iris::Renderer::Options::kUseValidationLayers |
          iris::Renderer::Options::kEnableRayTracing,
        0, {console_sink, file_sink});
      !result) {
    logger.critical("cannot initialize renderer: {}", result.error().what());
    std::exit(EXIT_FAILURE);
  }

  logger.info("Renderer initialized. {} files specified on command line.",
              files.size());

  if (auto cq = iris::Renderer::AcquireCommandQueue()) {
    sCommandQueue = std::move(*cq);
  } else {
    logger.critical("cannot acquire command queue: {}", cq.error().what());
    std::exit(EXIT_FAILURE);
  }

  VkDescriptorPool descriptorPool;
  VkDescriptorSetLayout setLayout;
  VkDescriptorSet set;

  if (auto pls = CreateDescriptor()) {
    std::tie(descriptorPool, setLayout, set) = *pls;
  } else {
    logger.critical("cannot create descriptor: {}", pls.error().what());
    std::exit(EXIT_FAILURE);
  }

  VkPipelineLayout layout;
  VkPipeline pipeline;

  if (auto lp = CreatePipeline(setLayout)) {
    std::tie(layout, pipeline) = *lp;
  } else {
    logger.critical("cannot create pipeline: {}", lp.error().what());
    std::exit(EXIT_FAILURE);
  }

  VkAccelerationStructureNV bottomLeveAS, topLevelAS;

  if (auto as = CreateAccelerationStructures(logger)) {
    std::tie(bottomLeveAS, topLevelAS) = *as;
  } else {
    logger.critical("cannot create acceleration structures: {}",
                    as.error().what());
    std::exit(EXIT_FAILURE);
  }

  VkBuffer matricesBuffer{VK_NULL_HANDLE};
  VmaAllocation matricesBufferAllocation{VK_NULL_HANDLE};
  VkDeviceSize matricesBufferSize{0};

  if (auto bas = iris::Renderer::CreateOrResizeBuffer(
        iris::Renderer::sAllocator, matricesBuffer, matricesBufferAllocation,
        matricesBufferSize, sizeof(Matrices),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU)) {
    std::tie(matricesBuffer, matricesBufferAllocation, matricesBufferSize) =
      *bas;
  } else {
    logger.critical("Cannot create matrices buffer: {}", bas.error().what());
    std::exit(EXIT_FAILURE);
  }

  VkImage outputImage;
  VmaAllocation outputImageAllocation;
  VkImageView outputImageView;

  if (auto iav = iris::Renderer::AllocateImageAndView(
        iris::Renderer::sDevice, iris::Renderer::sAllocator,
        VK_FORMAT_R8G8B8A8_UNORM, {1000, 1000}, 1, 1, VK_SAMPLE_COUNT_1_BIT,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_IMAGE_TILING_OPTIMAL, VMA_MEMORY_USAGE_GPU_ONLY,
        {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1})) {
    std::tie(outputImage, outputImageAllocation, outputImageView) = *iav;
  } else {
    logger.critical("cannot create output image: {}", iav.error().what());
    std::exit(EXIT_FAILURE);
  }

  absl::FixedArray<VkAccelerationStructureNV, 2> accelerationStructures{
    bottomLeveAS, topLevelAS};

  VkWriteDescriptorSetAccelerationStructureNV writeDescriptorSetAS = {};
  writeDescriptorSetAS.sType =
    VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_NV;
  writeDescriptorSetAS.accelerationStructureCount =
    gsl::narrow_cast<std::uint32_t>(accelerationStructures.size());
  writeDescriptorSetAS.pAccelerationStructures = accelerationStructures.data();

  VkDescriptorImageInfo imageInfo = {};
  imageInfo.imageView = outputImageView;
  imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

  VkDescriptorBufferInfo bufferInfo = {};
  bufferInfo.buffer = matricesBuffer;
  bufferInfo.offset = 0;
  bufferInfo.range = VK_WHOLE_SIZE;

  absl::FixedArray<VkWriteDescriptorSet, 3> descriptorWrites{{
    {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, &writeDescriptorSetAS, set, 0, 0,
     1, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV, nullptr, nullptr,
     nullptr},
    {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 1, 0, 1,
     VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &imageInfo, nullptr, nullptr},
    {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 2, 0, 1,
     VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &bufferInfo, nullptr},
  }};

  vkUpdateDescriptorSets(
    iris::Renderer::sDevice,
    gsl::narrow_cast<std::uint32_t>(descriptorWrites.size()),
    descriptorWrites.data(), 0, nullptr);

  VkPhysicalDeviceRayTracingPropertiesNV rayTracingProperties = {};
  rayTracingProperties.sType =
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PROPERTIES_NV;

  VkPhysicalDeviceProperties2 physicalDeviceProperties = {};
  physicalDeviceProperties.sType =
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
  physicalDeviceProperties.pNext = &rayTracingProperties;

  vkGetPhysicalDeviceProperties2(iris::Renderer::sPhysicalDevice,
                                 &physicalDeviceProperties);

  logger.info("shaderGroupHandleSize: {}",
              rayTracingProperties.shaderGroupHandleSize);
  std::uint32_t numGroups = 3;

  absl::FixedArray<std::byte> shaderGroupHandles(
    rayTracingProperties.shaderGroupHandleSize * numGroups);

  if (auto result = vkGetRayTracingShaderGroupHandlesNV(
        iris::Renderer::sDevice, pipeline, 0 /* firstGroup */,
        numGroups /* groupCount */, shaderGroupHandles.size(),
        shaderGroupHandles.data());
      result != VK_SUCCESS) {
    logger.critical("cannot get shader group handle: {}",
                    iris::Renderer::to_string(result));
    std::exit(EXIT_FAILURE);
  }

  VkBuffer sbtBuffer{VK_NULL_HANDLE};
  VmaAllocation sbtAllocation{VK_NULL_HANDLE};
  VkDeviceSize sbtBufferSize{0};

  if (auto bas = iris::Renderer::CreateOrResizeBuffer(
        iris::Renderer::sAllocator, sbtBuffer, sbtAllocation, sbtBufferSize,
        rayTracingProperties.shaderGroupHandleSize * numGroups,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU)) {
    std::tie(sbtBuffer, sbtAllocation, sbtBufferSize) = *bas;
  } else {
    logger.critical("cannot create sbt: {}", bas.error().what());
    std::exit(EXIT_FAILURE);
  }

  if (auto p = iris::Renderer::MapMemory<std::byte*>(iris::Renderer::sAllocator,
                                                    sbtAllocation)) {
    std::memcpy(shaderGroupHandles.data(), *p, sbtBufferSize);
  } else {
    logger.critical("cannot map sbt: {}", p.error().what());
    std::exit(EXIT_FAILURE);
  }

  vmaUnmapMemory(iris::Renderer::sAllocator, sbtAllocation);

  for (auto&& file : files) {
    logger.info("Loading {}", file);
    if (auto result = iris::Renderer::LoadFile(file); !result) {
      logger.error("Error loading {}: {}", file, result.error().what());
    }
  }

  int currentCBIndex = 0;

  auto commandBuffers = iris::Renderer::AllocateCommandBuffers(
    VK_COMMAND_BUFFER_LEVEL_PRIMARY, 2);
  if (!commandBuffers) {
    logger.error("Error allocating command buffers: {}",
                 commandBuffers.error().what());
    std::exit(EXIT_FAILURE);
  }

  VkFenceCreateInfo fenceCI = {};
  fenceCI.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceCI.flags = VK_FENCE_CREATE_SIGNALED_BIT;

  absl::FixedArray<VkFence> traceCompleteFences(commandBuffers->size());
  for (std::size_t i = 0; i < traceCompleteFences.size(); ++i) {
    if (auto result = vkCreateFence(iris::Renderer::sDevice, &fenceCI, nullptr,
                                    &traceCompleteFences[i]);
        result != VK_SUCCESS) {
      logger.error("Error creating fence: {}",
                   iris::Renderer::to_string(result));
      std::exit(EXIT_FAILURE);
    }
  }

  VkCommandBufferBeginInfo commandBufferBI = {};
  commandBufferBI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  commandBufferBI.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

  std::uint64_t frameCount = 0;

  while (iris::Renderer::IsRunning()) {
    iris::Renderer::BeginFrame();

    vkWaitForFences(iris::Renderer::sDevice, 1,
                    &traceCompleteFences[currentCBIndex], VK_TRUE, UINT64_MAX);
    vkResetFences(iris::Renderer::sDevice, 1,
                  &traceCompleteFences[currentCBIndex]);

    VkCommandBuffer cb = (*commandBuffers)[currentCBIndex];
    vkBeginCommandBuffer(cb, &commandBufferBI);

    VkDebugUtilsLabelEXT cbLabel = {};
    cbLabel.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;

    cbLabel.pLabelName = "readyBarrier";
    vkCmdBeginDebugUtilsLabelEXT(cb, &cbLabel);
    logger.info("readyBarrier");

    VkImageSubresourceRange sr = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    VkImageMemoryBarrier readyBarrier = {};
    readyBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    readyBarrier.srcAccessMask = 0;
    readyBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    readyBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    readyBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    readyBarrier.srcQueueFamilyIndex = readyBarrier.dstQueueFamilyIndex =
      VK_QUEUE_FAMILY_IGNORED;
    readyBarrier.image = outputImage;
    readyBarrier.subresourceRange = sr;

    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &readyBarrier);

    cbLabel.pLabelName = "trace";
    vkCmdBeginDebugUtilsLabelEXT(cb, &cbLabel);
    logger.info("trace");

    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_RAY_TRACING_NV, pipeline);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_RAY_TRACING_NV, layout,
                            0, 1, &set, 0, nullptr);

    VkDeviceSize rayGenOffset = 0;
    VkDeviceSize missOffset = rayTracingProperties.shaderGroupHandleSize;
    VkDeviceSize missStride = rayTracingProperties.shaderGroupHandleSize;
    VkDeviceSize hitGroupOffset = rayTracingProperties.shaderGroupHandleSize;
    VkDeviceSize hitGroupStride = rayTracingProperties.shaderGroupHandleSize;

    vkCmdTraceRaysNV(cb, sbtBuffer, rayGenOffset, sbtBuffer, missOffset,
                     missStride, sbtBuffer, hitGroupOffset, hitGroupStride,
                     VK_NULL_HANDLE, 0, 0, 1000, 1000, 1);

    cbLabel.pLabelName = "tracedBarrier";
    vkCmdBeginDebugUtilsLabelEXT(cb, &cbLabel);
    logger.info("tracedBarrier");

    VkImageMemoryBarrier tracedBarrier = {};
    tracedBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    tracedBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    tracedBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    tracedBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    tracedBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    tracedBarrier.srcQueueFamilyIndex = tracedBarrier.dstQueueFamilyIndex =
      VK_QUEUE_FAMILY_IGNORED;
    tracedBarrier.image = outputImage;
    tracedBarrier.subresourceRange = sr;

    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &tracedBarrier);
    vkEndCommandBuffer(cb);

    logger.info("submit");
    VkSubmitInfo submitI = {};
    submitI.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitI.commandBufferCount = 1;
    submitI.pCommandBuffers = &cb;

    if (auto result = vkQueueSubmit(sCommandQueue.queue, 1, &submitI,
                                    traceCompleteFences[currentCBIndex]);
        result != VK_SUCCESS) {
      logger.error("Error submitting command buffer: {}",
                   iris::Renderer::to_string(result));
    }

    iris::Renderer::EndFrame(outputImage);
    currentCBIndex = (currentCBIndex + 1) % 2;
    frameCount++;
  }

  logger.info("exiting");
}

