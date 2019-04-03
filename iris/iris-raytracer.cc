#include "iris/config.h"

#include "absl/container/fixed_array.h"
#include "absl/debugging/failure_signal_handler.h"
#include "absl/debugging/symbolize.h"
#include "fmt/format.h"
#include "glm/mat4x4.hpp"
#include "iris/acceleration_structure.h"
#include "iris/buffer.h"
#include "iris/image.h"
#include "iris/io/read_file.h"
#include "iris/pipeline.h"
#include "iris/protos.h"
#include "iris/renderer.h"
#include "iris/renderer_util.h"
#include "iris/shader.h"
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

static iris::Buffer sSpheresBuffer;

static iris::Renderer::CommandQueue sCommandQueue;
static VkDescriptorPool sDescriptorPool;
static VkDescriptorSetLayout sDescriptorSetLayout;
static VkDescriptorSet sDescriptorSet;
static iris::Pipeline sPipeline;
static iris::AccelerationStructure sBottomLevelAS;
static iris::AccelerationStructure sTopLevelAS;
static iris::Image sOutputImage;
static VkImageView sOutputImageView;
static std::uint32_t sShaderGroupHandleSize;
static iris::Buffer sShaderBindingTable;

static tl::expected<void, std::system_error> AcquireCommandQueue() noexcept {
  if (auto cq = iris::Renderer::AcquireCommandQueue()) {
    sCommandQueue = std::move(*cq);
  } else {
    using namespace std::string_literals;
    return tl::unexpected(
      std::system_error(cq.error().code(),
                        "Cannot acquire command queue: "s + cq.error().what()));
  }

  return {};
} // AcquireCommandQueue

static tl::expected<void, std::system_error> CreateDescriptor() noexcept {
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

  if (auto result = vkCreateDescriptorPool(iris::Renderer::sDevice, &poolCI,
                                           nullptr, &sDescriptorPool);
      result != VK_SUCCESS) {
    return tl::unexpected(std::system_error(iris::make_error_code(result),
                                            "Cannot create descriptor pool"));
  }

  absl::FixedArray<VkDescriptorSetLayoutBinding, 3> bindings{{
    {0, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV, 1,
     VK_SHADER_STAGE_RAYGEN_BIT_NV, nullptr},
    {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_RAYGEN_BIT_NV,
     nullptr},
    {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
     VK_SHADER_STAGE_RAYGEN_BIT_NV | VK_SHADER_STAGE_INTERSECTION_BIT_NV,
     nullptr},
  }};

  VkDescriptorSetLayoutCreateInfo layoutCI = {};
  layoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layoutCI.bindingCount = gsl::narrow_cast<std::uint32_t>(bindings.size());
  layoutCI.pBindings = bindings.data();

  if (auto result = vkCreateDescriptorSetLayout(
        iris::Renderer::sDevice, &layoutCI, nullptr, &sDescriptorSetLayout);
      result != VK_SUCCESS) {
    return tl::unexpected(std::system_error(
      iris::make_error_code(result), "Cannot create descriptor set layout"));
  }

  VkDescriptorSetAllocateInfo setAI = {};
  setAI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  setAI.descriptorPool = sDescriptorPool;
  setAI.descriptorSetCount = 1;
  setAI.pSetLayouts = &sDescriptorSetLayout;

  if (auto result = vkAllocateDescriptorSets(iris::Renderer::sDevice, &setAI,
                                             &sDescriptorSet);
      result != VK_SUCCESS) {
    return tl::unexpected(std::system_error(iris::make_error_code(result),
                                            "Cannot allocate descriptor set"));
  }

  return {};
} // CreateDescriptor

static tl::expected<void, std::system_error> CreatePipeline() noexcept {
  using namespace std::string_literals;
  absl::FixedArray<iris::Shader> shaders(4);

  if (auto rgen = iris::LoadShaderFromFile(
        iris::kIRISContentDirectory + "/assets/shaders/raytracing/raygen.glsl"s,
        VK_SHADER_STAGE_RAYGEN_BIT_NV)) {
    shaders[0] = std::move(*rgen);
  } else {
    return tl::unexpected(std::system_error(
      rgen.error().code(), "Cannot load raygen.glsl: "s + rgen.error().what()));
  }

  if (auto rmiss = iris::LoadShaderFromFile(
        iris::kIRISContentDirectory + "/assets/shaders/raytracing/miss.glsl"s,
        VK_SHADER_STAGE_MISS_BIT_NV)) {
    shaders[1] = std::move(*rmiss);
  } else {
    return tl::unexpected(std::system_error(
      rmiss.error().code(), "Cannot load miss.glsl: "s + rmiss.error().what()));
  }

  if (auto rchit = iris::LoadShaderFromFile(
        iris::kIRISContentDirectory +
          "/assets/shaders/raytracing/closest_hit.glsl"s,
        VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV)) {
    shaders[2] = std::move(*rchit);
  } else {
    return tl::unexpected(std::system_error(rchit.error().code(),
                                            "Cannot load closest_hit.glsl: "s +
                                              rchit.error().what()));
  }

  if (auto rint = iris::LoadShaderFromFile(
        iris::kIRISContentDirectory +
          "/assets/shaders/raytracing/sphere_intersect.glsl"s,
        VK_SHADER_STAGE_INTERSECTION_BIT_NV)) {
    shaders[3] = std::move(*rint);
  } else {
    return tl::unexpected(std::system_error(
      rint.error().code(),
      "Cannot load sphere_intersect.glsl: "s + rint.error().what()));
  }

  absl::FixedArray<iris::ShaderGroup> groups{
    {VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_NV, 0, 0, 0, 0},
    {VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_NV, 1, 0, 0, 0},
    {VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_NV, 0, 2, 0, 3},
  };

  if (auto pipe = iris::CreateRayTracingPipeline(
        shaders, groups, gsl::make_span(&sDescriptorSetLayout, 1), 2)) {
    sPipeline = std::move(*pipe);
  } else {
    return tl::unexpected(pipe.error());
  }

  return {};
} // CreatePipeline

static tl::expected<void, std::system_error> CreateSpheres() noexcept {
  if (auto buf = iris::CreateBuffer(
        sCommandQueue.commandPool, sCommandQueue.queue,
        sCommandQueue.submitFence, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY, sSpheres.size() * sizeof(Sphere),
        reinterpret_cast<std::byte*>(sSpheres.data()))) {
    sSpheresBuffer = std::move(*buf);
  } else {
    using namespace std::string_literals;
    return tl::unexpected(
      std::system_error(buf.error().code(), "Cannot create spheres buffer: "s +
                                              buf.error().what()));
  }

  return {};
} // CreateSpheres

static tl::expected<void, std::system_error> CreateOutputImage() noexcept {
  if (auto img = iris::AllocateImage(
        VK_FORMAT_R8G8B8A8_UNORM, {1000, 1000}, 1, 1, VK_SAMPLE_COUNT_1_BIT,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_IMAGE_TILING_OPTIMAL, VMA_MEMORY_USAGE_GPU_ONLY)) {
    sOutputImage = std::move(*img);
  } else {
    using namespace std::string_literals;
    return tl::unexpected(
      std::system_error(img.error().code(),
                        "cannot create output image: "s + img.error().what()));
  }

  if (auto view = iris::CreateImageView(
        sOutputImage, VK_IMAGE_VIEW_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM,
        {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1})) {
    sOutputImageView = *view;
  } else {
    using namespace std::string_literals;
    return tl::unexpected(std::system_error(
      view.error().code(),
      "cannot create output image view: "s + view.error().what()));
  }

  return {};
} // CreateOutputImage

static tl::expected<void, std::system_error>
CreateBottomLevelAccelerationStructure() noexcept {
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

  if (auto structure =
        iris::CreateAccelerationStructure(gsl::make_span(&geometry, 1), 0)) {
    sBottomLevelAS = std::move(*structure);
  } else {
    return tl::unexpected(std::system_error(structure.error().code(),
                                            "Cannot create bottom level AS: "s +
                                              structure.error().what()));
  }

  if (auto result = iris::BuildAccelerationStructure(
        sBottomLevelAS, sCommandQueue.commandPool, sCommandQueue.queue,
        sCommandQueue.submitFence);
      !result) {
    return tl::unexpected(
      std::system_error(result.error().code(),
                        "Cannot build topLevelAS: "s + result.error().what()));
  }

  return {};
} // CreateBottomLevelAccelerationStructure

static tl::expected<void, std::system_error>
CreateTopLevelAccelerationStructure() noexcept {
  using namespace std::string_literals;

  iris::GeometryInstance topLevelInstance(sBottomLevelAS.handle);

  if (auto structure = iris::CreateAccelerationStructure(1, 0)) {
    sTopLevelAS = std::move(*structure);
  } else {
    return tl::unexpected(std::system_error(structure.error().code(),
                                            "Cannot create top level AS: "s +
                                              structure.error().what()));
  }

  auto instanceBuffer = iris::AllocateBuffer(sizeof(iris::GeometryInstance),
                                             VK_BUFFER_USAGE_RAY_TRACING_BIT_NV,
                                             VMA_MEMORY_USAGE_GPU_ONLY);
  if (!instanceBuffer) {
    return tl::unexpected(
      std::system_error(instanceBuffer.error().code(),
                        "Cannot allocate instance buffer memory: "s +
                          instanceBuffer.error().what()));
  }

  if (auto ptr = instanceBuffer->Map<iris::GeometryInstance*>()) {
    **ptr = topLevelInstance;
    instanceBuffer->Unmap();
  } else {
    DestroyBuffer(*instanceBuffer);
    return tl::unexpected(
      std::system_error(ptr.error().code(), "Cannot map instance buffer: "s +
                                              instanceBuffer.error().what()));
  }

  if (auto result = iris::BuildAccelerationStructure(
        sTopLevelAS, sCommandQueue.commandPool, sCommandQueue.queue,
        sCommandQueue.submitFence, instanceBuffer->buffer);
      !result) {
    DestroyBuffer(*instanceBuffer);
    return tl::unexpected(
      std::system_error(result.error().code(),
                        "Cannot build topLevelAS: "s + result.error().what()));
  }

  DestroyBuffer(*instanceBuffer);

  return {};
} // CreateTopLevelAccelerationStructure

static tl::expected<void, std::system_error> WriteDescriptorSets() noexcept {
  absl::FixedArray<VkAccelerationStructureNV, 2> accelerationStructures{
    sBottomLevelAS.structure, sTopLevelAS.structure};

  VkWriteDescriptorSetAccelerationStructureNV writeDescriptorSetAS = {};
  writeDescriptorSetAS.sType =
    VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_NV;
  writeDescriptorSetAS.accelerationStructureCount =
    gsl::narrow_cast<std::uint32_t>(accelerationStructures.size());
  writeDescriptorSetAS.pAccelerationStructures = accelerationStructures.data();

  VkDescriptorImageInfo imageInfo = {};
  imageInfo.imageView = sOutputImageView;
  imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

  VkDescriptorBufferInfo bufferInfo = {};
  bufferInfo.buffer = sSpheresBuffer.buffer;
  bufferInfo.offset = 0;
  bufferInfo.range = sizeof(Sphere) * sSpheres.size();

  absl::FixedArray<VkWriteDescriptorSet, 2> descriptorWrites{{
    {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, &writeDescriptorSetAS,
     sDescriptorSet, 0, 0, 1, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV,
     nullptr, nullptr, nullptr},
    {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, sDescriptorSet, 1, 0, 1,
     VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &imageInfo, nullptr, nullptr},
    {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, sDescriptorSet, 2, 0, 1,
     VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &bufferInfo, nullptr},
  }};

  vkUpdateDescriptorSets(
    iris::Renderer::sDevice,
    gsl::narrow_cast<std::uint32_t>(descriptorWrites.size()),
    descriptorWrites.data(), 0, nullptr);

  return {};
} // WriteDescriptorSets

static tl::expected<void, std::system_error>
CreateShaderBindingTable() noexcept {
  VkPhysicalDeviceRayTracingPropertiesNV rayTracingProperties = {};
  rayTracingProperties.sType =
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PROPERTIES_NV;

  VkPhysicalDeviceProperties2 physicalDeviceProperties = {};
  physicalDeviceProperties.sType =
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
  physicalDeviceProperties.pNext = &rayTracingProperties;

  vkGetPhysicalDeviceProperties2(iris::Renderer::sPhysicalDevice,
                                 &physicalDeviceProperties);
  sShaderGroupHandleSize = rayTracingProperties.shaderGroupHandleSize;

  std::uint32_t numGroups = 3;

  absl::FixedArray<std::byte> shaderGroupHandles(
    rayTracingProperties.shaderGroupHandleSize * numGroups);

  if (auto result = vkGetRayTracingShaderGroupHandlesNV(
        iris::Renderer::sDevice, sPipeline.pipeline, 0 /* firstGroup */,
        numGroups /* groupCount */, shaderGroupHandles.size(),
        shaderGroupHandles.data());
      result != VK_SUCCESS) {
    return tl::unexpected(std::system_error(iris::make_error_code(result),
                                            "Cannot get shader group handles"));
  }

  if (auto buf = iris::AllocateBuffer(sShaderGroupHandleSize * numGroups,
                                      VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                      VMA_MEMORY_USAGE_CPU_TO_GPU)) {
    sShaderBindingTable = std::move(*buf);
  } else {
    using namespace std::string_literals;
    return tl::unexpected(std::system_error(buf.error().code(),
          "Cannot create shader binding table: "s + buf.error().what()));
  }

  if (auto ptr = sShaderBindingTable.Map<std::byte*>()) {
    std::memcpy(shaderGroupHandles.data(), *ptr, sShaderBindingTable.size);
    sShaderBindingTable.Unmap();
  } else {
    using namespace std::string_literals;
    return tl::unexpected(std::system_error(
      ptr.error().code(),
      "Cannot map shader binding table: "s + ptr.error().what()));
  }

  return {};
} // CreateShaderBindingTable

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
                      "iris-raytracer",
                      iris::Renderer::Options::kReportDebugMessages |
                        iris::Renderer::Options::kUseValidationLayers,
                      {console_sink, file_sink}, 0)
                      .and_then(AcquireCommandQueue)
                      .and_then(CreateDescriptor)
                      .and_then(CreatePipeline)
                      .and_then(CreateSpheres)
                      .and_then(CreateOutputImage)
                      .and_then(CreateBottomLevelAccelerationStructure)
                      .and_then(CreateTopLevelAccelerationStructure)
                      .and_then(WriteDescriptorSets)
                      .and_then(CreateShaderBindingTable);
      !result) {
    logger.critical("initialization failed: {}", result.error().what());
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
                 iris::to_string(result));
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
      logger.error("Error creating fence: {}", iris::to_string(result));
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
    readyBarrier.image = sOutputImage.image;
    readyBarrier.subresourceRange = sr;

    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &readyBarrier);

    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_RAY_TRACING_NV,
                      sPipeline.pipeline);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_RAY_TRACING_NV,
                            sPipeline.layout, 0, 1, &sDescriptorSet, 0,
                            nullptr);

    VkDeviceSize rayGenOffset = 0;
    VkDeviceSize missOffset = sShaderGroupHandleSize;
    VkDeviceSize missStride = sShaderGroupHandleSize;
    VkDeviceSize hitGroupOffset = sShaderGroupHandleSize * 2;
    VkDeviceSize hitGroupStride = sShaderGroupHandleSize;

    vkCmdTraceRaysNV(cb, sShaderBindingTable.buffer, rayGenOffset,
                     sShaderBindingTable.buffer, missOffset, missStride,
                     sShaderBindingTable.buffer, hitGroupOffset, hitGroupStride,
                     VK_NULL_HANDLE, 0, 0, 1000, 1000, 1);

    VkImageMemoryBarrier tracedBarrier = {};
    tracedBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    tracedBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    tracedBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    tracedBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    tracedBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    tracedBarrier.srcQueueFamilyIndex = tracedBarrier.dstQueueFamilyIndex =
      VK_QUEUE_FAMILY_IGNORED;
    tracedBarrier.image = sOutputImage.image;
    tracedBarrier.subresourceRange = sr;

    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &tracedBarrier);
    vkEndCommandBuffer(cb);

    VkSubmitInfo submitI = {};
    submitI.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitI.commandBufferCount = 1;
    submitI.pCommandBuffers = &cb;

    if (auto result = vkQueueSubmit(sCommandQueue.queue, 1, &submitI,
                                    traceCompleteFences[currentCBIndex]);
        result != VK_SUCCESS) {
      logger.error("Error submitting command buffer: {}",
                   iris::to_string(result));
    }

    iris::Renderer::EndFrame(sOutputImage.image);
    currentCBIndex = (currentCBIndex + 1) % 2;
    frameCount++;
  }

  logger.info("exiting");
}
