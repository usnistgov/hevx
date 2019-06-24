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
  Sphere(glm::vec3(0.f, 0.f, 0.f), .5f),
  Sphere(glm::vec3(0.f, 0.f, 100.5f), 100.f),
};

static iris::Buffer sSpheresBuffer;

static iris::Renderer::CommandQueue sCommandQueue;
absl::InlinedVector<iris::ShaderGroup, 4> sShaderGroups;
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
  setAI.descriptorPool = iris::Renderer::sDescriptorPool;
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

  if (auto rint = iris::LoadShaderFromFile(
        iris::kIRISContentDirectory + "/assets/shaders/sphere.rint"s,
        VK_SHADER_STAGE_INTERSECTION_BIT_NV)) {
    shaders[2] = std::move(*rint);
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(rint.error());
  }

  if (auto rchit = iris::LoadShaderFromFile(
        iris::kIRISContentDirectory + "/assets/shaders/sphere.rchit"s,
        VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV)) {
    shaders[3] = std::move(*rchit);
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(rchit.error());
  }

  sShaderGroups.push_back(iris::ShaderGroup::General(0));
  sShaderGroups.push_back(iris::ShaderGroup::General(1));
  sShaderGroups.push_back(iris::ShaderGroup::ProceduralHit(2, 3));

  if (auto pipe = iris::CreateRayTracingPipeline(
        shaders,       // shaders
        sShaderGroups, // groups
        gsl::make_span(&sTraceable.descriptorSetLayout,
                       1), // descriptorSetLayouts
        4                  // maxRecursionDepth
        )) {
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
        VK_FORMAT_R8G8B8A8_UNORM, sTraceable.outputImageExtent, 1, 1,
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
        sTraceable.outputImage, VK_IMAGE_VIEW_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM,
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
CreateGeometry() noexcept {
  IRIS_LOG_ENTER();

  VkGeometryTrianglesNV triangles = {};
  triangles.sType = VK_STRUCTURE_TYPE_GEOMETRY_TRIANGLES_NV;

  VkGeometryAABBNV spheres = {};
  spheres.sType = VK_STRUCTURE_TYPE_GEOMETRY_AABB_NV;
  spheres.pNext = nullptr;
  spheres.aabbData = sSpheresBuffer.buffer;
  spheres.numAABBs = gsl::narrow_cast<std::uint32_t>(sSpheres.size());
  spheres.stride = sizeof(Sphere);
  spheres.offset = offsetof(Sphere, aabbMin);

  sTraceable.geometry.sType = VK_STRUCTURE_TYPE_GEOMETRY_NV;
  sTraceable.geometry.flags = VK_GEOMETRY_OPAQUE_BIT_NV;
  sTraceable.geometry.geometryType = VK_GEOMETRY_TYPE_AABBS_NV;
  sTraceable.geometry.flags = VK_GEOMETRY_OPAQUE_BIT_NV;
  sTraceable.geometry.geometry.triangles = triangles;
  sTraceable.geometry.geometry.aabbs = spheres;

  IRIS_LOG_LEAVE();
  return {};
} // CreateGeometry

static tl::expected<void, std::system_error>
CreateBottomLevelAccelerationStructure() noexcept {
  IRIS_LOG_ENTER();

  using namespace std::string_literals;

  if (auto structure = iris::CreateAccelerationStructure(
        gsl::make_span(&sTraceable.geometry, 1), 0)) {
    sTraceable.bottomLevelAccelerationStructure = std::move(*structure);
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(structure.error().code(),
                                            "Cannot create bottom level AS: "s +
                                              structure.error().what()));
  }

  IRIS_LOG_LEAVE();
  return {};
} // CreateBottomLevelAccelerationStructure

static tl::expected<void, std::system_error>
CreateInstance() noexcept {
  IRIS_LOG_ENTER();

  sTraceable.instance =
    iris::GeometryInstance(sTraceable.bottomLevelAccelerationStructure.handle);

  IRIS_LOG_LEAVE();
  return {};
} // CreateInstance

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
    {
      VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,       // sType
      &writeDescriptorSetAS,                        // pNext
      sTraceable.descriptorSet,                     // dstSet
      0,                                            // dstBinding
      0,                                            // dstArrayElement
      1,                                            // descriptorCount
      VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV, // descriptorType
      nullptr,                                      // pImageInfo
      nullptr,                                      // pBufferInfo
      nullptr                                       // pTexelBufferView
    },
    {
      VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, // sType
      nullptr,                                // pNext
      sTraceable.descriptorSet,               // dstSet
      1,                                      // dstBinding
      0,                                      // dstArrayElement
      1,                                      // descriptorCount
      VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,       // descriptorType
      &imageInfo,                             // pImageInfo
      nullptr,                                // pBufferInfo
      nullptr                                 // pTexelBufferView
    },
    {
      VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, // sType
      nullptr,                                // pNext
      sTraceable.descriptorSet,               // dstSet
      2,                                      // dstBinding
      0,                                      // dstArrayElement
      1,                                      // descriptorCount
      VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,      // descriptorType
      nullptr,                                // pImageInfo
      &bufferInfo,                            // pBufferInfo
      nullptr                                 // pTexelBufferView
    },
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

  absl::FixedArray<std::byte> shaderGroupHandles(shaderGroupHandleSize *
                                                 sShaderGroups.size());

  if (auto result = vkGetRayTracingShaderGroupHandlesNV(
        iris::Renderer::sDevice,                               // device
        sTraceable.pipeline.pipeline,                          // pipeline
        0,                                                     // firstGroup
        gsl::narrow_cast<std::uint32_t>(sShaderGroups.size()), // groupCount
        gsl::narrow_cast<std::uint32_t>(shaderGroupHandles.size()), // dataSize
        shaderGroupHandles.data()                                   // pData
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

  IRIS_LOG_LEAVE();
  return {};
} // CreateShaderBindingTable

static tl::expected<void, std::system_error> CreateTraceFence() noexcept {
  IRIS_LOG_ENTER();

  VkFenceCreateInfo fenceCI = {};
  fenceCI.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceCI.flags = VK_FENCE_CREATE_SIGNALED_BIT;

  if (auto result = vkCreateFence(iris::Renderer::sDevice, &fenceCI, nullptr,
                                  &sTraceable.traceCompleteFence);
      result != VK_SUCCESS) {
    sLogger->error("Error creating fence: {}", iris::to_string(result));
    std::exit(EXIT_FAILURE);
  }

  IRIS_LOG_LEAVE();
  return {};
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
                      .and_then(CreateDescriptor)
                      .and_then(CreatePipeline)
                      .and_then(CreateOutputImage)
                      .and_then(CreateSpheres)
                      .and_then(CreateGeometry)
                      .and_then(CreateBottomLevelAccelerationStructure)
                      .and_then(CreateInstance)
                      .and_then(CreateTopLevelAccelerationStructure)
                      .and_then(WriteDescriptorSets)
                      .and_then(CreateShaderBindingTable)
                      .and_then(CreateTraceFence);
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

  iris::Renderer::Nav::Reposition({0.f, 2.f, 0.f});
  iris::Renderer::AddTraceable(sTraceable);
  while (iris::Renderer::IsRunning()) {
    iris::Renderer::BeginFrame();
    iris::Renderer::EndFrame();
  }

  sLogger->info("exiting");
}
