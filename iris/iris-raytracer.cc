#include "iris/config.h"

#include "absl/container/fixed_array.h"
#include "absl/debugging/failure_signal_handler.h"
#include "absl/debugging/symbolize.h"
#include "absl/flags/parse.h"
#include "fmt/format.h"
#include "glm/mat4x4.hpp"
#include "iris/acceleration_structure.h"
#include "iris/buffer.h"
#include "iris/components/traceable.h"
#include "iris/image.h"
#include "iris/io/read_file.h"
#include "iris/logging.h"
#include "iris/pipeline.h"
#include "iris/protos.h"
#include "iris/renderer.h"
#include "iris/renderer_private.h"
#include "iris/shader.h"
#include "spdlog/logger.h"
#include "spdlog/sinks/ansicolor_sink.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include <cstdlib>
#include <exception>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

static std::shared_ptr<spdlog::logger> sLogger;

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
  Sphere(glm::vec3(0.f, 2.f, 0.f), .5f),
  Sphere(glm::vec3(0.f, 2.f, 100.5f), 100.f),
};

static iris::Buffer sSpheresBuffer;

static iris::Renderer::CommandQueue sCommandQueue;
static VkDescriptorPool sDescriptorPool;
static iris::Renderer::Component::Traceable sTraceable;

static tl::expected<void, std::system_error> AcquireCommandQueue() noexcept {
  IRIS_LOG_ENTER();

  if (auto cq = iris::Renderer::AcquireCommandQueue()) {
    sCommandQueue = std::move(*cq);
  } else {
    using namespace std::string_literals;
    return tl::unexpected(
      std::system_error(cq.error().code(),
                        "Cannot acquire command queue: "s + cq.error().what()));
  }

  IRIS_LOG_LEAVE();
  return {};
} // AcquireCommandQueue

static tl::expected<void, std::system_error> CreateDescriptorPool() noexcept {
  IRIS_LOG_ENTER();

  absl::FixedArray<VkDescriptorPoolSize, 4> poolSizes{{
    {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV, 32},
    {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 32},
    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 32},
  }};

  VkDescriptorPoolCreateInfo poolCI = {};
  poolCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  poolCI.maxSets = 32;
  poolCI.poolSizeCount = gsl::narrow_cast<std::uint32_t>(poolSizes.size());
  poolCI.pPoolSizes = poolSizes.data();

  if (auto result = vkCreateDescriptorPool(iris::Renderer::sDevice, &poolCI,
                                           nullptr, &sDescriptorPool);
    result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(iris::make_error_code(result),
                                            "Cannot create descriptor pool"));
  }

  IRIS_LOG_LEAVE();
  return {};
} // CreateDescriptorPool

static tl::expected<void, std::system_error> CreateDescriptor() noexcept {
  IRIS_LOG_ENTER();

  absl::FixedArray<VkDescriptorSetLayoutBinding, 4> bindings{
    {
      0,                                            // binding
      VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV, // descriptorType
      1,                                            // descriptorCount
      VK_SHADER_STAGE_RAYGEN_BIT_NV |
        VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV, // stageFlags
      nullptr                               // pImmutableSamplers
    },
    {
      1,                                // binding
      VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, // descriptorType
      1,                                // descriptorCount
      VK_SHADER_STAGE_RAYGEN_BIT_NV,    // stageFlags
      nullptr                           // pImmutableSamplers
    },
    {
      2,                                 // binding
      VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, // descriptorType
      1,                                 // descriptorCount
      VK_SHADER_STAGE_INTERSECTION_BIT_NV |
        VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV, // stageFlags
      nullptr                               // pImmutableSamplers
    },
  };

  VkDescriptorSetLayoutCreateInfo layoutCI = {};
  layoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layoutCI.bindingCount = gsl::narrow_cast<std::uint32_t>(bindings.size());
  layoutCI.pBindings = bindings.data();

  if (auto result =
        vkCreateDescriptorSetLayout(iris::Renderer::sDevice, &layoutCI, nullptr,
                                    &sTraceable.descriptorSetLayout);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(
      iris::make_error_code(result), "Cannot create descriptor set layout"));
  }

  VkDescriptorSetAllocateInfo setAI = {};
  setAI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  setAI.descriptorPool = sDescriptorPool;
  setAI.descriptorSetCount = 1;
  setAI.pSetLayouts = &sTraceable.descriptorSetLayout;

  if (auto result = vkAllocateDescriptorSets(iris::Renderer::sDevice, &setAI,
                                             &sTraceable.descriptorSet);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(iris::make_error_code(result),
                                            "Cannot allocate descriptor set"));
  }

  IRIS_LOG_LEAVE();
  return {};
} // CreateDescriptor

static tl::expected<void, std::system_error> CreatePipeline() noexcept {
  IRIS_LOG_ENTER();
  Expects((iris::Renderer::AvailableFeatures() &
           iris::Renderer::Features::kRayTracing) ==
          iris::Renderer::Features::kRayTracing);

  using namespace std::string_literals;
  absl::FixedArray<iris::Shader> shaders(4);

  if (auto rgen = iris::LoadShaderFromFile(iris::kIRISContentDirectory +
                                             "/assets/shaders/raygen.rgen"s,
                                           VK_SHADER_STAGE_RAYGEN_BIT_NV)) {
    shaders[0] = std::move(*rgen);
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(rgen.error());
  }

  if (auto rmiss = iris::LoadShaderFromFile(iris::kIRISContentDirectory +
                                              "/assets/shaders/miss.rmiss"s,
                                            VK_SHADER_STAGE_MISS_BIT_NV)) {
    shaders[1] = std::move(*rmiss);
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(rmiss.error());
  }

  if (auto rchit = iris::LoadShaderFromFile(
        iris::kIRISContentDirectory + "/assets/shaders/sphere.rchit"s,
        VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV)) {
    shaders[2] = std::move(*rchit);
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(rchit.error());
  }

  if (auto rint = iris::LoadShaderFromFile(
        iris::kIRISContentDirectory + "/assets/shaders/sphere.rint"s,
        VK_SHADER_STAGE_INTERSECTION_BIT_NV)) {
    shaders[3] = std::move(*rint);
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(rint.error());
  }

  absl::FixedArray<iris::ShaderGroup> groups{
    iris::ShaderGroup::General(0), iris::ShaderGroup::General(1),
    iris::ShaderGroup::ProceduralHit(3, 2)};

  if (auto pipe = iris::CreateRayTracingPipeline(
        shaders, groups, gsl::make_span(&sTraceable.descriptorSetLayout, 1),
        4)) {
    sTraceable.pipeline = std::move(*pipe);
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(pipe.error());
  }

  IRIS_LOG_LEAVE();
  return {};
} // CreatePipeline

static tl::expected<void, std::system_error> CreateOutputImage() noexcept {
  IRIS_LOG_ENTER();

  sTraceable.outputImageExtent = VkExtent2D{1000, 1000};

  if (auto img = iris::AllocateImage(
        VK_FORMAT_B8G8R8A8_UNORM, sTraceable.outputImageExtent, 1, 1,
        VK_SAMPLE_COUNT_1_BIT,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
          VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_IMAGE_TILING_OPTIMAL, VMA_MEMORY_USAGE_GPU_ONLY)) {
    sTraceable.outputImage = std::move(*img);
  } else {
    IRIS_LOG_LEAVE();
    using namespace std::string_literals;
    return tl::unexpected(
      std::system_error(img.error().code(),
                        "cannot create output image: "s + img.error().what()));
  }

  if (auto view = iris::CreateImageView(
        sTraceable.outputImage, VK_IMAGE_VIEW_TYPE_2D, VK_FORMAT_B8G8R8A8_UNORM,
        {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1})) {
    sTraceable.outputImageView = *view;
  } else {
    IRIS_LOG_LEAVE();
    using namespace std::string_literals;
    return tl::unexpected(std::system_error(
      view.error().code(),
      "cannot create output image view: "s + view.error().what()));
  }

  IRIS_LOG_LEAVE();
  return {};
} // CreateOutputImage

static tl::expected<void, std::system_error> CreateSpheres() noexcept {
  IRIS_LOG_ENTER();

  if (auto buf = iris::CreateBuffer(
        sCommandQueue.commandPool, sCommandQueue.queue,
        sCommandQueue.submitFence, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY, sSpheres.size() * sizeof(Sphere),
        reinterpret_cast<std::byte*>(sSpheres.data()))) {
    sSpheresBuffer = std::move(*buf);
  } else {
    IRIS_LOG_LEAVE();
    using namespace std::string_literals;
    return tl::unexpected(
      std::system_error(buf.error().code(), "Cannot create spheres buffer: "s +
                                              buf.error().what()));
  }

  IRIS_LOG_LEAVE();
  return {};
} // CreateSpheres

static tl::expected<void, std::system_error>
CreateBottomLevelAccelerationStructure() noexcept {
  IRIS_LOG_ENTER();

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
    sTraceable.bottomLevelAccelerationStructure = std::move(*structure);
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(structure.error().code(),
                                            "Cannot create bottom level AS: "s +
                                              structure.error().what()));
  }

  if (auto result = iris::BuildAccelerationStructure(
        sTraceable.bottomLevelAccelerationStructure, sCommandQueue.commandPool,
        sCommandQueue.queue, sCommandQueue.submitFence);
      !result) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(result.error().code(),
                        "Cannot build topLevelAS: "s + result.error().what()));
  }

  IRIS_LOG_LEAVE();
  return {};
} // CreateBottomLevelAccelerationStructure

static tl::expected<void, std::system_error>
CreateTopLevelAccelerationStructure() noexcept {
  IRIS_LOG_ENTER();
  using namespace std::string_literals;

  if (auto structure = iris::CreateAccelerationStructure(1, 0)) {
    sTraceable.topLevelAccelerationStructure = std::move(*structure);
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(structure.error().code(),
                                            "Cannot create top level AS: "s +
                                              structure.error().what()));
  }

  iris::GeometryInstance topLevelInstance(
    sTraceable.bottomLevelAccelerationStructure.handle);

  auto instanceBuffer = iris::AllocateBuffer(sizeof(iris::GeometryInstance),
                                             VK_BUFFER_USAGE_RAY_TRACING_BIT_NV,
                                             VMA_MEMORY_USAGE_CPU_TO_GPU);
  if (!instanceBuffer) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(instanceBuffer.error().code(),
                        "Cannot allocate instance buffer memory: "s +
                          instanceBuffer.error().what()));
  }

  if (auto ptr = instanceBuffer->Map<iris::GeometryInstance*>()) {
    std::memcpy(*ptr, &topLevelInstance, sizeof(iris::GeometryInstance));
    instanceBuffer->Unmap();
  } else {
    DestroyBuffer(*instanceBuffer);
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(ptr.error().code(), "Cannot map instance buffer: "s +
                                              instanceBuffer.error().what()));
  }

  if (auto result = iris::BuildAccelerationStructure(
        sTraceable.topLevelAccelerationStructure, sCommandQueue.commandPool,
        sCommandQueue.queue, sCommandQueue.submitFence, instanceBuffer->buffer);
      !result) {
    DestroyBuffer(*instanceBuffer);
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(result.error().code(),
                        "Cannot build topLevelAS: "s + result.error().what()));
  }

  DestroyBuffer(*instanceBuffer);

  IRIS_LOG_LEAVE();
  return {};
} // CreateTopLevelAccelerationStructure

static tl::expected<void, std::system_error> WriteDescriptorSets() noexcept {
  IRIS_LOG_ENTER();

  VkWriteDescriptorSetAccelerationStructureNV writeDescriptorSetAS = {};
  writeDescriptorSetAS.sType =
    VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_NV;
  writeDescriptorSetAS.accelerationStructureCount = 1;
  writeDescriptorSetAS.pAccelerationStructures =
    &sTraceable.topLevelAccelerationStructure.structure;

  VkDescriptorImageInfo imageInfo = {};
  imageInfo.imageView = sTraceable.outputImageView;
  imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

  VkDescriptorBufferInfo bufferInfo = {};
  bufferInfo.buffer = sSpheresBuffer.buffer;
  bufferInfo.offset = 0;
  bufferInfo.range = sizeof(Sphere) * sSpheres.size();

  absl::FixedArray<VkWriteDescriptorSet, 3> descriptorWrites{
    {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, &writeDescriptorSetAS,
     sTraceable.descriptorSet, 0, 0, 1,
     VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV, nullptr, nullptr, nullptr},
    {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, sTraceable.descriptorSet,
     1, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &imageInfo, nullptr, nullptr},
    {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, sTraceable.descriptorSet,
     2, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &bufferInfo, nullptr},
  };

  vkUpdateDescriptorSets(
    iris::Renderer::sDevice,
    gsl::narrow_cast<std::uint32_t>(descriptorWrites.size()),
    descriptorWrites.data(), 0, nullptr);

  IRIS_LOG_LEAVE();
  return {};
} // WriteDescriptorSets

static tl::expected<void, std::system_error>
CreateShaderBindingTable() noexcept {
  IRIS_LOG_ENTER();

  VkPhysicalDeviceRayTracingPropertiesNV rayTracingProperties = {};
  rayTracingProperties.sType =
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PROPERTIES_NV;

  VkPhysicalDeviceProperties2 physicalDeviceProperties = {};
  physicalDeviceProperties.sType =
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
  physicalDeviceProperties.pNext = &rayTracingProperties;

  vkGetPhysicalDeviceProperties2(iris::Renderer::sPhysicalDevice,
                                 &physicalDeviceProperties);
  VkDeviceSize const shaderGroupHandleSize =
    rayTracingProperties.shaderGroupHandleSize;

  absl::FixedArray<std::byte> shaderGroupHandles(shaderGroupHandleSize * 3);

  if (auto result = vkGetRayTracingShaderGroupHandlesNV(
        iris::Renderer::sDevice,      // device
        sTraceable.pipeline.pipeline, // pipeline
        0,                            // firstGroup
        3,                            // groupCount
        shaderGroupHandles.size(),    // dataSize
        shaderGroupHandles.data()     // pData
      );
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(iris::make_error_code(result),
                                            "Cannot get shader group handles"));
  }

  if (auto buf = iris::CreateBuffer(
        sCommandQueue.commandPool, sCommandQueue.queue,
        sCommandQueue.submitFence, VK_BUFFER_USAGE_RAY_TRACING_BIT_NV,
        VMA_MEMORY_USAGE_GPU_ONLY, shaderGroupHandleSize * 3,
        shaderGroupHandles.data())) {
    sTraceable.shaderBindingTable = std::move(*buf);
  } else {
    using namespace std::string_literals;
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(
      buf.error().code(),
      "Cannot create shader binding table: "s + buf.error().what()));
  }

  sTraceable.missBindingOffset = shaderGroupHandleSize;
  sTraceable.missBindingStride = shaderGroupHandleSize;
  sTraceable.hitBindingOffset =
    sTraceable.missBindingOffset + sTraceable.missBindingStride;
  sTraceable.hitBindingStride = shaderGroupHandleSize;

  sLogger->info("shaderGroupHandleSize: {}", shaderGroupHandleSize);
  sLogger->info("sTraceable.missBindingOffset: {}",
                sTraceable.missBindingOffset);
  sLogger->info("sTraceable.missBindingStride: {}",
                sTraceable.missBindingStride);
  sLogger->info("sTraceable.hitBindingOffset: {}", sTraceable.hitBindingOffset);
  sLogger->info("sTraceable.hitBindingStride: {}", sTraceable.hitBindingStride);

  for (int i = 0; i < 3; ++i) {
    std::string handle(shaderGroupHandleSize, '\0');
    for (std::uint32_t j = 0; j < shaderGroupHandleSize; ++j) {
      sprintf(handle.data() + j, "%0x",
              static_cast<char>(*(shaderGroupHandles.data() +
                                  (i * shaderGroupHandleSize) + j)));
    }

    sLogger->info("groupIndex: {} handle: 0x{}", i, handle);
  }

  IRIS_LOG_LEAVE();
  return {};
} // CreateShaderBindingTable

static tl::expected<void, std::system_error>
CreateTraceSemaphore() noexcept {
  IRIS_LOG_ENTER();

  VkSemaphoreCreateInfo semaphoreCI = {};
  semaphoreCI.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

  if (auto result =
        vkCreateSemaphore(iris::Renderer::sDevice, &semaphoreCI, nullptr,
                          &sTraceable.traceCompleteSemaphore);
      result != VK_SUCCESS) {
    sLogger->error("Error creating semaphore: {}", iris::to_string(result));
    std::exit(EXIT_FAILURE);
  }

  IRIS_LOG_LEAVE();
  return {};
} // CreateTraceSemaphore

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

  auto const positional = absl::ParseCommandLine(argc, argv);

  auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
    "iris-raytracer.log", true);
  file_sink->set_level(spdlog::level::trace);

  auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
  console_sink->set_level(spdlog::level::trace);

  sLogger = std::shared_ptr<spdlog::logger>(
    new spdlog::logger("iris-viewer", {console_sink, file_sink}));
  sLogger->set_level(spdlog::level::trace);

  sLogger->info("Logging initialized");

  if (auto result = iris::Renderer::Initialize(
                      "iris-raytracer",
                      iris::Renderer::Options::kReportDebugMessages |
                        iris::Renderer::Options::kEnableValidation,
                      {console_sink, file_sink}, 0)
                      .and_then(AcquireCommandQueue)
                      .and_then(CreateDescriptorPool)
                      .and_then(CreateDescriptor)
                      .and_then(CreatePipeline)
                      .and_then(CreateOutputImage)
                      .and_then(CreateSpheres)
                      .and_then(CreateBottomLevelAccelerationStructure)
                      .and_then(CreateTopLevelAccelerationStructure)
                      .and_then(WriteDescriptorSets)
                      .and_then(CreateShaderBindingTable)
                      .and_then(CreateTraceSemaphore);
      !result) {
    sLogger->critical("initialization failed: {}", result.error().what());
    std::exit(EXIT_FAILURE);
  }

  for (size_t i = 1; i < positional.size(); ++i) {
    sLogger->info("Loading {}", positional[i]);
    if (auto result = iris::Renderer::LoadFile(positional[i]); !result) {
      sLogger->error("Error loading {}: {}", positional[i],
                   result.error().what());
    }
  }

  std::uint64_t frameCount = 0;

  iris::Renderer::AddTraceable(sTraceable);

  while (iris::Renderer::IsRunning()) {
    iris::Renderer::BeginFrame();

    iris::Renderer::EndFrame();
    frameCount++;
  }

  sLogger->info("exiting");
}

