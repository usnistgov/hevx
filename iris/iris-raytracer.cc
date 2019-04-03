#include "iris/config.h"

#include "absl/debugging/failure_signal_handler.h"
#include "absl/debugging/symbolize.h"
#include "fmt/format.h"
#include "glm/mat4x4.hpp"
#include "iris/buffer.h"
#include "iris/io/read_file.h"
#include "iris/protos.h"
#include "iris/renderer.h"
#include "iris/renderer_util.h"
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
static VkDescriptorPool sDescriptorPool;
static VkDescriptorSetLayout sDescriptorSetLayout;
static VkDescriptorSet sDescriptorSet;
static iris::Renderer::Pipeline sPipeline;
static iris::Renderer::AccelerationStructure sBottomLevelAS;
static iris::Renderer::AccelerationStructure sTopLevelAS;

struct Sphere {
  glm::vec3 aabbMin;
  glm::vec3 aabbMax;

  Sphere(glm::vec3 center, float radius) noexcept
    : aabbMin(center - glm::vec3(radius))
    , aabbMax(center + glm::vec3(radius)) {}

  glm::vec3 center() const noexcept {
    return (aabbMax + aabbMin) / glm::vec3(2.f);
  }

  float radius() const noexcept { return (aabbMax.x - aabbMin.x) / 2.f; }
}; // struct Sphere

static absl::FixedArray<Sphere> sSpheres = {
  Sphere(glm::vec3(0.f, 0.f, 0.f), .5f),
  Sphere(glm::vec3(0.f, -100.5f, 0.f), 100.f),
};

static iris::Renderer::Buffer sSpheresBuffer;

tl::expected<
  std::tuple<VkDescriptorPool, VkDescriptorSetLayout, VkDescriptorSet>,
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

tl::expected<iris::Renderer::Pipeline, std::system_error>
CreatePipeline(VkDescriptorSetLayout setLayout) noexcept {
  using namespace std::string_literals;

  auto rayGen = iris::Renderer::LoadShaderFromFile(
    iris::kIRISContentDirectory + "/assets/shaders/raytracing/raygen.glsl"s,
    VK_SHADER_STAGE_RAYGEN_BIT_NV);
  if (!rayGen) {
    return tl::unexpected(
      std::system_error(rayGen.error().code(),
                        "Cannot load raygen.glsl: "s + rayGen.error().what()));
  }

  auto miss = iris::Renderer::LoadShaderFromFile(
    iris::kIRISContentDirectory + "/assets/shaders/raytracing/miss.glsl"s,
    VK_SHADER_STAGE_MISS_BIT_NV);
  if (!miss) {
    return tl::unexpected(std::system_error(
      miss.error().code(), "Cannot load miss.glsl: "s + miss.error().what()));
  }

  auto closestHit = iris::Renderer::LoadShaderFromFile(
    iris::kIRISContentDirectory +
      "/assets/shaders/raytracing/closest_hit.glsl"s,
    VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV);
  if (!closestHit) {
    return tl::unexpected(std::system_error(closestHit.error().code(),
                                            "Cannot load closest_hit.glsl: "s +
                                              closestHit.error().what()));
  }

  auto sphereIntersect = iris::Renderer::LoadShaderFromFile(
    iris::kIRISContentDirectory +
      "/assets/shaders/raytracing/sphere_intersect.glsl"s,
    VK_SHADER_STAGE_INTERSECTION_BIT_NV);
  if (!sphereIntersect) {
    return tl::unexpected(std::system_error(
      sphereIntersect.error().code(),
      "Cannot load sphere_intersect.glsl: "s + sphereIntersect.error().what()));
  }

  absl::FixedArray<iris::Renderer::Shader> shaders{
    {*rayGen, VK_SHADER_STAGE_RAYGEN_BIT_NV},
    {*miss, VK_SHADER_STAGE_MISS_BIT_NV},
    {*closestHit, VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV},
    {*sphereIntersect, VK_SHADER_STAGE_INTERSECTION_BIT_NV},
  };

  absl::FixedArray<iris::Renderer::ShaderGroup> groups{
    {VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_NV, 0, 0, 0, 0},
    {VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_NV, 1, 0, 0, 0},
    {VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_NV, 0, 2, 0, 3},
  };

  return iris::Renderer::CreateRayTracingPipeline(
    shaders, groups, gsl::make_span(&setLayout, 1), 2);
} // CreatePipeline

tl::expected<iris::Renderer::AccelerationStructure, std::system_error>
CreateBottomLevelAccelerationStructure(spdlog::logger& logger) noexcept {
  using namespace std::string_literals;

  VkGeometryTrianglesNV triangles = {};
  triangles.sType = VK_STRUCTURE_TYPE_GEOMETRY_TRIANGLES_NV;

  VkGeometryAABBNV spheres = {};
  spheres.sType = VK_STRUCTURE_TYPE_GEOMETRY_AABB_NV;
  spheres.pNext = nullptr;
  spheres.aabbData = sSpheresBuffer.buffer;
  spheres.numAABBs = gsl::narrow_cast<std::uint32_t>(sSpheres.size());
  spheres.stride = sizeof(Sphere);
  spheres.offset = offsetof(Sphere, aabbMin);

  VkGeometryNV geometry = {};
  geometry.sType = VK_STRUCTURE_TYPE_GEOMETRY_NV;
  geometry.flags = VK_GEOMETRY_OPAQUE_BIT_NV;
  geometry.geometryType = VK_GEOMETRY_TYPE_AABBS_NV;
  geometry.flags = VK_GEOMETRY_OPAQUE_BIT_NV;
  geometry.geometry.triangles = triangles;
  geometry.geometry.aabbs = spheres;

  VkAccelerationStructureInfoNV asInfo = {};
  asInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_INFO_NV;
  asInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_NV;
  asInfo.flags = 0;
  asInfo.instanceCount = 0;
  asInfo.geometryCount = 1;
  asInfo.pGeometries = &geometry;

  auto structure = iris::Renderer::CreateAccelerationStructure(asInfo, 0);
  if (!structure) {
    return tl::unexpected(std::system_error(structure.error().code(),
                                            "Cannot create bottom level AS: "s +
                                              structure.error().what()));
  }

  VkMemoryRequirements2KHR memoryRequirements = {};
  memoryRequirements.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2_KHR;

  VkAccelerationStructureMemoryRequirementsInfoNV memoryRequirementsInfo = {};
  memoryRequirementsInfo.sType =
    VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_NV;
  memoryRequirementsInfo.accelerationStructure = structure->structure;
  memoryRequirementsInfo.type =
    VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_BUILD_SCRATCH_NV;
  vkGetAccelerationStructureMemoryRequirementsNV(
    iris::Renderer::sDevice, &memoryRequirementsInfo, &memoryRequirements);

  auto scratch = iris::Renderer::AllocateBuffer(
    memoryRequirements.memoryRequirements.size,
    VK_BUFFER_USAGE_RAY_TRACING_BIT_NV, VMA_MEMORY_USAGE_GPU_ONLY);
  if (!scratch) {
    return tl::unexpected(std::system_error(
      scratch.error().code(), "Cannot allocate bottom level scratch memory: "s +
                                scratch.error().what()));
  }

  VkCommandBuffer commandBuffer;
  if (auto cb = iris::Renderer::BeginOneTimeSubmit(sCommandQueue.commandPool)) {
    commandBuffer = *cb;
  } else {
    DestroyBuffer(*scratch);
    return tl::unexpected(std::system_error(cb.error()));
  }

  logger.info("vkCmdBuildAccelerationStructureNV bottomLevelAS");
  vkCmdBuildAccelerationStructureNV(commandBuffer, &asInfo,
                                    VK_NULL_HANDLE,       // instanceData
                                    0,                    // instanceOffset
                                    VK_FALSE,             // update
                                    structure->structure, // dst
                                    VK_NULL_HANDLE,       // src
                                    scratch->buffer,      // scratchBuffer
                                    0                     // scratchOffset
  );

  logger.info("EndOneTimeSubmit bottomLevelAS");
  if (auto result = iris::Renderer::EndOneTimeSubmit(
        commandBuffer, sCommandQueue.commandPool, sCommandQueue.queue,
        sCommandQueue.submitFence);
      !result) {
    DestroyBuffer(*scratch);
    return tl::unexpected(std::system_error(
      result.error().code(),
      "Cannot build acceleration struture: "s + result.error().what()));
  }

  DestroyBuffer(*scratch);

  return std::move(*structure);
} // CreateBottomLevelAccelerationStructure

tl::expected<iris::Renderer::AccelerationStructure, std::system_error>
CreateTopLevelAccelerationStructure(
  spdlog::logger& logger,
  gsl::span<iris::Renderer::GeometryInstance> instances) noexcept {
  using namespace std::string_literals;

  VkAccelerationStructureInfoNV asInfo = {};
  asInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_INFO_NV;
  asInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_NV;
  asInfo.instanceCount = 1;
  asInfo.geometryCount = 0;
  asInfo.pGeometries = nullptr;

  auto structure = iris::Renderer::CreateAccelerationStructure(asInfo, 0);
  if (!structure) {
    return tl::unexpected(std::system_error(structure.error().code(),
                                            "Cannot create top level AS: "s +
                                              structure.error().what()));
  }

  auto instanceBuffer = iris::Renderer::AllocateBuffer(
    sizeof(iris::Renderer::GeometryInstance) * instances.size(),
    VK_BUFFER_USAGE_RAY_TRACING_BIT_NV, VMA_MEMORY_USAGE_GPU_ONLY);
  if (!instanceBuffer) {
    return tl::unexpected(
      std::system_error(instanceBuffer.error().code(),
                        "Cannot allocate instance buffer memory: "s +
                          instanceBuffer.error().what()));
  }

  logger.info("Created instance buffer for topLevelAS sized: {}",
              instanceBuffer->size);

  if (auto ptr = instanceBuffer->Map<iris::Renderer::GeometryInstance*>()) {
    for (auto&& instance : instances) {
      std::memcpy(*ptr, &instance, sizeof(iris::Renderer::GeometryInstance));
      (*ptr)++;
    }
    instanceBuffer->Unmap();
  } else {
    DestroyBuffer(*instanceBuffer);
    return tl::unexpected(
      std::system_error(ptr.error().code(), "Cannot map instance buffer: "s +
                                              instanceBuffer.error().what()));
  }

  VkMemoryRequirements2KHR memoryRequirements = {};
  memoryRequirements.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2_KHR;

  VkAccelerationStructureMemoryRequirementsInfoNV memoryRequirementsInfo = {};
  memoryRequirementsInfo.sType =
    VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_NV;
  memoryRequirementsInfo.accelerationStructure = structure->structure;
  memoryRequirementsInfo.type =
    VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_BUILD_SCRATCH_NV;
  vkGetAccelerationStructureMemoryRequirementsNV(
    iris::Renderer::sDevice, &memoryRequirementsInfo, &memoryRequirements);

  logger.info("Creating scratch buffer for topLevelAS sized: {}",
              memoryRequirements.memoryRequirements.size);
  auto scratch = iris::Renderer::AllocateBuffer(
    memoryRequirements.memoryRequirements.size,
    VK_BUFFER_USAGE_RAY_TRACING_BIT_NV, VMA_MEMORY_USAGE_GPU_ONLY);
  if (!scratch) {
    return tl::unexpected(std::system_error(scratch.error().code(),
                                            "Cannot allocate build memory: "s +
                                              scratch.error().what()));
  }

  VkCommandBuffer commandBuffer;
  if (auto cb = iris::Renderer::BeginOneTimeSubmit(sCommandQueue.commandPool)) {
    commandBuffer = *cb;
  } else {
    DestroyBuffer(*instanceBuffer);
    DestroyBuffer(*scratch);
    return tl::unexpected(std::system_error(cb.error()));
  }

  logger.info("vkCmdBuildAccelerationStructureNV topLevelAS");
  vkCmdBuildAccelerationStructureNV(commandBuffer, &asInfo,
                                    instanceBuffer->buffer, // instanceData
                                    0,                      // instanceOffset
                                    VK_FALSE,               // update
                                    structure->structure,   // dst
                                    VK_NULL_HANDLE,         // dst
                                    scratch->buffer,        // scratchBuffer
                                    0                       // scratchOffset
  );

  if (auto result = iris::Renderer::EndOneTimeSubmit(
        commandBuffer, sCommandQueue.commandPool, sCommandQueue.queue,
        sCommandQueue.submitFence);
      !result) {
    DestroyBuffer(*instanceBuffer);
    DestroyBuffer(*scratch);
    return tl::unexpected(std::system_error(
      result.error().code(),
      "Cannot build acceleration struture: "s + result.error().what()));
  }

  DestroyBuffer(*instanceBuffer);
  DestroyBuffer(*scratch);

  return std::move(*structure);
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
          iris::Renderer::Options::kUseValidationLayers,
        {console_sink, file_sink}, 0);
      !result) {
    logger.critical("cannot initialize renderer: {}", result.error().what());
    std::exit(EXIT_FAILURE);
  }

  if ((iris::Renderer::AvailableFeatures() &
       iris::Renderer::Features::kRayTracing) !=
      iris::Renderer::Features::kRayTracing) {
    logger.critical("cannot initialize renderer: raytracing not supported");
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

  if (auto pls = CreateDescriptor()) {
    std::tie(sDescriptorPool, sDescriptorSetLayout, sDescriptorSet) = *pls;
  } else {
    logger.critical("cannot create descriptor: {}", pls.error().what());
    std::exit(EXIT_FAILURE);
  }

  if (auto pipe = CreatePipeline(sDescriptorSetLayout)) {
    sPipeline = std::move(*pipe);
  } else {
    logger.critical("cannot create pipeline: {}", pipe.error().what());
    std::exit(EXIT_FAILURE);
  }

  if (auto buf = iris::Renderer::CreateBuffer(
        sCommandQueue.commandPool, sCommandQueue.queue,
        sCommandQueue.submitFence, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY, sSpheres.size() * sizeof(Sphere),
        reinterpret_cast<std::byte*>(sSpheres.data()))) {
    sSpheresBuffer = std::move(*buf);
  } else {
    logger.critical("cannot create spheres buffer: {}", buf.error().what());
    std::exit(EXIT_FAILURE);
  }

  if (auto as = CreateBottomLevelAccelerationStructure(logger)) {
    sBottomLevelAS = std::move(*as);
  } else {
    logger.critical("cannot create bottom level acceleration structure: {}",
                    as.error().what());
    std::exit(EXIT_FAILURE);
  }

  iris::Renderer::GeometryInstance topLevelInstance;

  if (auto result = vkGetAccelerationStructureHandleNV(
        iris::Renderer::sDevice, sBottomLevelAS.structure,
        sizeof(topLevelInstance.accelerationStructureHandle),
        &topLevelInstance.accelerationStructureHandle);
      result != VK_SUCCESS) {
    logger.critical("cannot get bottom level acceleration structure handle: {}",
                    iris::Renderer::to_string(result));
    std::exit(EXIT_FAILURE);
  }

  if (auto as = CreateTopLevelAccelerationStructure(
        logger, gsl::make_span(&topLevelInstance, 1))) {
    sTopLevelAS = std::move(*as);
  } else {
    logger.critical("cannot create top level acceleration structure: {}",
                    as.error().what());
    std::exit(EXIT_FAILURE);
  }

  iris::Renderer::Image outputImage;
  VkImageView outputImageView;

  if (auto img = iris::Renderer::AllocateImage(
        VK_FORMAT_R8G8B8A8_UNORM, {1000, 1000}, 1, 1, VK_SAMPLE_COUNT_1_BIT,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_IMAGE_TILING_OPTIMAL, VMA_MEMORY_USAGE_GPU_ONLY)) {
    outputImage = std::move(*img);
  } else {
    logger.critical("cannot create output image: {}", img.error().what());
    std::exit(EXIT_FAILURE);
  }

  if (auto view = iris::Renderer::CreateImageView(
        outputImage, VK_IMAGE_VIEW_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM,
        {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1})) {
    outputImageView = *view;
  } else {
    logger.critical("cannot create output image view: {}", view.error().what());
    std::exit(EXIT_FAILURE);
  }

  absl::FixedArray<VkAccelerationStructureNV, 2> accelerationStructures{
    sBottomLevelAS.structure, sTopLevelAS.structure};

  VkWriteDescriptorSetAccelerationStructureNV writeDescriptorSetAS = {};
  writeDescriptorSetAS.sType =
    VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_NV;
  writeDescriptorSetAS.accelerationStructureCount =
    gsl::narrow_cast<std::uint32_t>(accelerationStructures.size());
  writeDescriptorSetAS.pAccelerationStructures = accelerationStructures.data();

  VkDescriptorImageInfo imageInfo = {};
  imageInfo.imageView = outputImageView;
  imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

  absl::FixedArray<VkWriteDescriptorSet, 2> descriptorWrites{{
    {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, &writeDescriptorSetAS,
     sDescriptorSet, 0, 0, 1, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV,
     nullptr, nullptr, nullptr},
    {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, sDescriptorSet, 1, 0, 1,
     VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &imageInfo, nullptr, nullptr},
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
        iris::Renderer::sDevice, sPipeline.pipeline, 0 /* firstGroup */,
        numGroups /* groupCount */, shaderGroupHandles.size(),
        shaderGroupHandles.data());
      result != VK_SUCCESS) {
    logger.critical("cannot get shader group handle: {}",
                    iris::Renderer::to_string(result));
    std::exit(EXIT_FAILURE);
  }

  iris::Renderer::Buffer sbtBuffer;

  if (auto buf = iris::Renderer::AllocateBuffer(
        rayTracingProperties.shaderGroupHandleSize * numGroups,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU)) {
    sbtBuffer = std::move(*buf);
  } else {
    logger.critical("cannot create sbt: {}", buf.error().what());
    std::exit(EXIT_FAILURE);
  }

  if (auto p = iris::Renderer::MapMemory<std::byte*>(sbtBuffer.allocation)) {
    std::memcpy(shaderGroupHandles.data(), *p, sbtBuffer.size);
    iris::Renderer::UnmapMemory(sbtBuffer.allocation);
  } else {
    logger.critical("cannot map sbt: {}", p.error().what());
    std::exit(EXIT_FAILURE);
  }

  for (auto&& file : files) {
    logger.info("Loading {}", file);
    if (auto result = iris::Renderer::LoadFile(file); !result) {
      logger.error("Error loading {}: {}", file, result.error().what());
    }
  }

  int currentCBIndex = 0;

  VkCommandBufferAllocateInfo commandBufferAI = {};
  commandBufferAI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  commandBufferAI.commandPool = sCommandQueue.commandPool;
  commandBufferAI.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  commandBufferAI.commandBufferCount = 2;

  absl::FixedArray<VkCommandBuffer> commandBuffers(2);
  if (auto result = vkAllocateCommandBuffers(
        iris::Renderer::sDevice, &commandBufferAI, commandBuffers.data());
      result != VK_SUCCESS) {
    logger.error("Cannot allocate command buffers: {}",
                 iris::Renderer::to_string(result));
    std::exit(EXIT_FAILURE);
  }

  VkFenceCreateInfo fenceCI = {};
  fenceCI.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceCI.flags = VK_FENCE_CREATE_SIGNALED_BIT;

  absl::FixedArray<VkFence> traceCompleteFences(commandBuffers.size());
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

    VkCommandBuffer cb = commandBuffers[currentCBIndex];
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
    readyBarrier.image = outputImage.image;
    readyBarrier.subresourceRange = sr;

    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &readyBarrier);

    cbLabel.pLabelName = "trace";
    vkCmdBeginDebugUtilsLabelEXT(cb, &cbLabel);
    logger.info("trace");

    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_RAY_TRACING_NV,
                      sPipeline.pipeline);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_RAY_TRACING_NV,
                            sPipeline.layout, 0, 1, &sDescriptorSet, 0,
                            nullptr);

    VkDeviceSize rayGenOffset = 0;
    VkDeviceSize missOffset = rayTracingProperties.shaderGroupHandleSize;
    VkDeviceSize missStride = rayTracingProperties.shaderGroupHandleSize;
    VkDeviceSize hitGroupOffset = rayTracingProperties.shaderGroupHandleSize;
    VkDeviceSize hitGroupStride = rayTracingProperties.shaderGroupHandleSize;

    vkCmdTraceRaysNV(cb, sbtBuffer.buffer, rayGenOffset, sbtBuffer.buffer,
                     missOffset, missStride, sbtBuffer.buffer, hitGroupOffset,
                     hitGroupStride, VK_NULL_HANDLE, 0, 0, 1000, 1000, 1);

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
    tracedBarrier.image = outputImage.image;
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

    iris::Renderer::EndFrame(outputImage.image);
    currentCBIndex = (currentCBIndex + 1) % 2;
    frameCount++;
  }

  logger.info("exiting");
}
