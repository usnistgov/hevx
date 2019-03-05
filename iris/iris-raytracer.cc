#include "iris/config.h"

#include "absl/debugging/failure_signal_handler.h"
#include "absl/debugging/symbolize.h"
#include "fmt/format.h"
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

void CreatePipeline(spdlog::logger& logger) {
  using namespace std::string_literals;

  VkShaderModule rayGenSM;
  if (auto sm = LoadShaderFromFile(iris::kIRISContentDirectory +
                                     "/assets/shaders/raytracing/raygen.glsl"s,
                                   VK_SHADER_STAGE_RAYGEN_BIT_NV)) {
    rayGenSM = *sm;
  } else {
    logger.error("Cannot load raygen.glsl: {}", sm.error().what());
    return;
  }

  VkShaderModule closestHitSM;
  if (auto sm =
        LoadShaderFromFile(iris::kIRISContentDirectory +
                             "/assets/shaders/raytracing/closest_hit.glsl"s,
                           VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV)) {
    closestHitSM = *sm;
  } else {
    logger.error("Cannot load closest_hit.glsl: {}", sm.error().what());
    return;
  }

  VkShaderModule sphereIntersectSM;
  if (auto sm = LoadShaderFromFile(
        iris::kIRISContentDirectory +
          "/assets/shaders/raytracing/sphere_intersect.glsl"s,
        VK_SHADER_STAGE_INTERSECTION_BIT_NV)) {
    sphereIntersectSM = *sm;
  } else {
    logger.error("Cannot load sphere_intersect.glsl: {}", sm.error().what());
    return;
  }

  absl::FixedArray<VkPipelineShaderStageCreateInfo, 3> shaderStages{
    VkPipelineShaderStageCreateInfo{
      VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
      VK_SHADER_STAGE_RAYGEN_BIT_NV, rayGenSM, "main", nullptr},
    VkPipelineShaderStageCreateInfo{
      VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
      VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV, closestHitSM, "main", nullptr},
    VkPipelineShaderStageCreateInfo{
      VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
      VK_SHADER_STAGE_INTERSECTION_BIT_NV, sphereIntersectSM, "main", nullptr},
  };

  VkRayTracingShaderGroupCreateInfoNV rayTracingShaderGroupCI = {};
  rayTracingShaderGroupCI.sType =
    VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_NV;
  rayTracingShaderGroupCI.type =
    VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_NV;

  // index of raygen, miss, callable shader
  rayTracingShaderGroupCI.generalShader = 0;
  // VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV
  rayTracingShaderGroupCI.closestHitShader = 1;
  // or index for VK_SHADER_STAGE_ANY_HIT_BIT_NV
  rayTracingShaderGroupCI.anyHitShader = VK_SHADER_UNUSED_NV;
  // VK_SHADER_STAGE_INTERSECTION_BIT_NV
  rayTracingShaderGroupCI.intersectionShader = 2;

  VkRayTracingPipelineCreateInfoNV rayTracingPipelineCI = {};
  rayTracingPipelineCI.sType =
    VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_NV;
  rayTracingPipelineCI.stageCount = shaderStages.size();
  rayTracingPipelineCI.pStages = shaderStages.data();
  rayTracingPipelineCI.groupCount = 1;
  rayTracingPipelineCI.pGroups = &rayTracingShaderGroupCI;
  rayTracingPipelineCI.maxRecursionDepth = 2;
  //rayTracingPipelineCI.layout = layout;
  (void)rayTracingPipelineCI;

} // CreatePipeline

void CreateAccelerationStructures(spdlog::logger& logger) noexcept {
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
    logger.error("Cannot create aabb buffer: {}", bas.error().what());
    return;
  }

  VkGeometryNV geometry = {};
  geometry.sType = VK_STRUCTURE_TYPE_GEOMETRY_NV;
  geometry.flags = VK_GEOMETRY_OPAQUE_BIT_NV;
  geometry.geometryType = VK_GEOMETRY_TYPE_AABBS_NV;
  geometry.geometry.aabbs.sType = VK_STRUCTURE_TYPE_GEOMETRY_AABB_NV;
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
    logger.error("Cannot create bottom level AS: {}", as.error().what());
    return;
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
    logger.error("Cannot create top level AS: {}", as.error().what());
    return;
  }
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
    "iris-viewer.log", true);
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

  CreatePipeline(logger);
  CreateAccelerationStructures(logger);

  for (auto&& file : files) {
    logger.info("Loading {}", file);
    if (auto result = iris::Renderer::LoadFile(file); !result) {
      logger.error("Error loading {}: {}", file, result.error().what());
    }
  }

  while (iris::Renderer::IsRunning()) {
    iris::Renderer::BeginFrame();

    iris::Renderer::EndFrame();
  }

  logger.info("exiting");
}

