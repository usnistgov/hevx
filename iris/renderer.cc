/*! \file
 * \brief \ref iris::Renderer definition.
 */
#include "config.h"

#include "absl/container/flat_hash_map.h"
#include "renderer.h"
#if PLATFORM_COMPILER_GCC
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#include "absl/container/inlined_vector.h"
#include "enumerate.h"
#include "error.h"
#include "glm/gtc/matrix_transform.hpp"
#if PLATFORM_COMPILER_GCC
#pragma GCC diagnostic ignored "-Wshadow"
#endif
#include "glslang/Public/ShaderLang.h"
#include "gsl/gsl"
#include "io/gltf.h"
#include "io/json.h"
#include "io/shadertoy.h"
#include "pipeline.h"
#include "protos.h"
#include "shader.h"
#include "renderer_util.h"
#if PLATFORM_COMPILER_MSVC
#pragma warning(push)
#pragma warning(disable : 4127)
#endif
#include "spdlog/spdlog.h"
#if PLATFORM_COMPILER_MSVC
#pragma warning(pop)
#endif
#include "tbb/concurrent_queue.h"
#include "tbb/task.h"
#include "tbb/task_scheduler_init.h"
#include "vulkan.h"
#include "vulkan_util.h"
#include "window.h"
#include "wsi/input.h"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

using namespace std::string_literals;

/////
//
// The logging must be directly defined here instead of including "logging.h".
// This is because we need to define the static logger instance in this file.
//
/////
namespace iris {

static spdlog::logger*
GetLogger(spdlog::sinks_init_list logSinks = {}) noexcept {
  static std::shared_ptr<spdlog::logger> sLogger;
  if (!sLogger) {
    sLogger = std::make_shared<spdlog::logger>("iris", logSinks);
    sLogger->set_level(spdlog::level::trace);
    spdlog::register_logger(sLogger);
    spdlog::set_pattern("[%Y-%m-%d %T.%e] [%t] [%n] %^[%l] %v%$");
  }

  return sLogger.get();
}

} // namespace iris

//! \brief Logs entry into a function.
#define IRIS_LOG_ENTER()                                                       \
  do {                                                                         \
    ::iris::GetLogger()->trace("ENTER: {} ({}:{})", __func__, __FILE__,        \
                               __LINE__);                                      \
    ::iris::GetLogger()->flush();                                              \
  } while (false)

//! \brief Logs leave from a function.
#define IRIS_LOG_LEAVE()                                                       \
  do {                                                                         \
    ::iris::GetLogger()->trace("LEAVE: {} ({}:{})", __func__, __FILE__,        \
                               __LINE__);                                      \
    ::iris::GetLogger()->flush();                                              \
  } while (false)

namespace iris::Renderer {

VkInstance sInstance{VK_NULL_HANDLE};
VkDebugUtilsMessengerEXT sDebugUtilsMessenger{VK_NULL_HANDLE};
VkPhysicalDevice sPhysicalDevice{VK_NULL_HANDLE};
VkDevice sDevice{VK_NULL_HANDLE};
VmaAllocator sAllocator{VK_NULL_HANDLE};
VkRenderPass sRenderPass{VK_NULL_HANDLE};

VkSurfaceFormatKHR const sSurfaceColorFormat{VK_FORMAT_B8G8R8A8_UNORM,
                                             VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
VkFormat const sSurfaceDepthStencilFormat{VK_FORMAT_D32_SFLOAT};
VkSampleCountFlagBits const sSurfaceSampleCount{VK_SAMPLE_COUNT_4_BIT};
VkPresentModeKHR const sSurfacePresentMode{VK_PRESENT_MODE_FIFO_KHR};

VkDescriptorPool sDescriptorPool{VK_NULL_HANDLE};
VkDescriptorSetLayout sGlobalDescriptorSetLayout{VK_NULL_HANDLE};
static VkDescriptorSet sGlobalDescriptorSet{VK_NULL_HANDLE};

VkQueryPool sTimestampsQueryPool{VK_NULL_HANDLE};

absl::flat_hash_map<std::string, iris::Window>& Windows() {
  static absl::flat_hash_map<std::string, iris::Window> sWindows;
  return sWindows;
} // Windows

std::uint32_t sQueueFamilyIndex{UINT32_MAX};
absl::InlinedVector<VkQueue, 16> sCommandQueues;
absl::InlinedVector<VkCommandPool, 16> sCommandPools;
absl::InlinedVector<VkFence, 16> sCommandFences;

static std::uint32_t sCommandQueueHead{1};
static std::uint32_t sCommandQueueFree{UINT32_MAX};
static std::timed_mutex sCommandQueueMutex;

std::uint32_t const sNumRenderPassAttachments{4};
std::uint32_t const sColorTargetAttachmentIndex{0};
std::uint32_t const sColorResolveAttachmentIndex{1};
std::uint32_t const sDepthStencilTargetAttachmentIndex{2};
std::uint32_t const sDepthStencilResolveAttachmentIndex{3};

// TODO: implement ID system so that multiple renderables work and can be removed
class Renderables {
public:
  std::vector<Component::Renderable> operator()() {
    std::lock_guard<std::mutex> lock{mutex_};
    return renderables_;
  }

  void push_back(Component::Renderable renderable) {
    std::lock_guard<std::mutex> lock{mutex_};
    renderables_.clear();
    renderables_.push_back(std::move(renderable));
  }

private:
  std::mutex mutex_{};
  std::vector<Component::Renderable> renderables_{};
}; // class Renderables

static Renderables sRenderables{};

static Buffer sMatricesBuffer;
static Buffer sLightsBuffer;

static Features sFeatures{Features::kNone};
static bool sRunning{false};
static bool sInFrame{false};
static std::uint32_t sFrameNum{0};
static std::chrono::steady_clock::time_point sPreviousFrameTime{};

static constexpr std::uint32_t const sNumWindowFramesBuffered{2};
static absl::FixedArray<VkFence> sFrameFinishedFences(sNumWindowFramesBuffered);

static VkSemaphore sImagesReadyForPresent{VK_NULL_HANDLE};
static std::uint32_t sFrameIndex{0};

static tbb::task_scheduler_init sTaskSchedulerInit{
  tbb::task_scheduler_init::deferred};
static tbb::concurrent_queue<std::function<std::system_error(void)>>
  sIOContinuations{};

static char const* sUIVertexShaderSource = R"(
#version 450 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;
layout(push_constant) uniform uPushConstant {
  vec2 uScale;
  vec2 uTranslate;
};
layout(location = 0) out vec4 Color;
layout(location = 1) out vec2 UV;
out gl_PerVertex {
  vec4 gl_Position;
};
void main() {
  Color = aColor;
  UV = aUV;
  gl_Position = vec4(aPos * uScale + uTranslate, 0.f, 1.f);
})";

static char const* sUIFragmentShaderSource = R"(
#version 450 core
layout(set = 1, binding = 0) uniform sampler sSampler;
layout(set = 1, binding = 1) uniform texture2D sTexture;
layout(location = 0) in vec4 Color;
layout(location = 1) in vec2 UV;
layout(location = 0) out vec4 fColor;
void main() {
  fColor = Color * texture(sampler2D(sTexture, sSampler), UV.st);
})";

static Pipeline sUIPipeline;
static VkDescriptorSetLayout sUIDescriptorSetLayout{VK_NULL_HANDLE};
static VkDescriptorSet sUIDescriptorSet{VK_NULL_HANDLE};

static void
CreateEmplaceWindow(iris::Control::Window const& windowMessage) noexcept {
  auto const& bg = windowMessage.background_color();

  Window::Options options = Window::Options::kNone;
  if (windowMessage.show_system_decoration()) {
    options |= Window::Options::kDecorated;
  }
  if (windowMessage.is_stereo()) options |= Window::Options::kStereo;
  if (windowMessage.show_ui()) options |= Window::Options::kShowUI;

  if (auto win = CreateWindow(
        windowMessage.name().c_str(),
        wsi::Offset2D{gsl::narrow_cast<std::int16_t>(windowMessage.x()),
                      gsl::narrow_cast<std::int16_t>(windowMessage.y())},
        wsi::Extent2D{gsl::narrow_cast<std::uint16_t>(windowMessage.width()),
                      gsl::narrow_cast<std::uint16_t>(windowMessage.height())},
        {bg.r(), bg.g(), bg.b(), bg.a()}, options, windowMessage.display(),
        sNumWindowFramesBuffered)) {
    Windows().emplace(windowMessage.name(), std::move(*win));
  } else {
    GetLogger()->warn("Creating window failed: {}", win.error().what());
  }
} // CreateEmplaceWindow

static VkCommandBuffer CopyImage(VkImage dst, VkImage src,
                                 VkExtent3D const& extent) noexcept {
  VkCommandBufferAllocateInfo commandBufferAI = {};
  commandBufferAI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  commandBufferAI.commandPool = sCommandPools[0];
  commandBufferAI.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
  commandBufferAI.commandBufferCount = 1;

  VkCommandBuffer commandBuffer;
  if (auto result =
        vkAllocateCommandBuffers(sDevice, &commandBufferAI, &commandBuffer);
      result != VK_SUCCESS) {
    GetLogger()->error("Cannot allocate command buffer: {}",
                       iris::to_string(result));
    return VK_NULL_HANDLE;
  }

  VkCommandBufferInheritanceInfo commandBufferII = {};
  commandBufferII.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;

  VkCommandBufferBeginInfo commandBufferBI = {};
  commandBufferBI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  commandBufferBI.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
  commandBufferBI.pInheritanceInfo = &commandBufferII;

  vkBeginCommandBuffer(commandBuffer, &commandBufferBI);

  SetImageLayout(
    commandBuffer, dst, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
    VK_PIPELINE_STAGE_TRANSFER_BIT, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT, 1, 1);

  VkImageCopy copy = {};
  copy.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  copy.srcOffset = {0, 0, 0};
  copy.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  copy.dstOffset = {0, 0, 0};
  copy.extent = extent;

  vkCmdCopyImage(commandBuffer, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst,
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

  SetImageLayout(
    commandBuffer, dst, VK_PIPELINE_STAGE_TRANSFER_BIT,
    VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_ASPECT_COLOR_BIT, 1, 1);

  vkEndCommandBuffer(commandBuffer);
  return commandBuffer;
} // CopyImage

static VkCommandBuffer
RenderRenderable(Component::Renderable const& renderable, VkViewport* pViewport,
                 VkRect2D* pScissor,
                 gsl::span<std::byte> pushConstants) noexcept {
  VkCommandBufferAllocateInfo commandBufferAI = {};
  commandBufferAI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  commandBufferAI.commandPool = sCommandPools[0];
  commandBufferAI.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
  commandBufferAI.commandBufferCount = 1;

  VkCommandBuffer commandBuffer;
  if (auto result =
        vkAllocateCommandBuffers(sDevice, &commandBufferAI, &commandBuffer);
      result != VK_SUCCESS) {
    GetLogger()->error("Cannot allocate command buffer: {}",
                       iris::to_string(result));
    return VK_NULL_HANDLE;
  }

  VkCommandBufferInheritanceInfo commandBufferII = {};
  commandBufferII.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
  commandBufferII.renderPass = sRenderPass;
  commandBufferII.subpass = 0;
  commandBufferII.framebuffer = VK_NULL_HANDLE;

  VkCommandBufferBeginInfo commandBufferBI = {};
  commandBufferBI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  commandBufferBI.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT |
                          VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
  commandBufferBI.pInheritanceInfo = &commandBufferII;

  vkBeginCommandBuffer(commandBuffer, &commandBufferBI);
  vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    renderable.pipeline.pipeline);

  vkCmdBindDescriptorSets(commandBuffer,                   // commandBuffer
                          VK_PIPELINE_BIND_POINT_GRAPHICS, // pipelineBindPoint
                          renderable.pipeline.layout,      // layout
                          0,                               // firstSet
                          1,                               // descriptorSetCount
                          &sGlobalDescriptorSet,           // pDescriptorSets
                          0,                               // dynamicOffsetCount
                          nullptr                          // pDynamicOffsets
  );

  vkCmdPushConstants(commandBuffer, renderable.pipeline.layout,
                     VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                     0, pushConstants.size(), pushConstants.data());

  vkCmdSetViewport(commandBuffer, 0, 1, pViewport);
  vkCmdSetScissor(commandBuffer, 0, 1, pScissor);

  if (renderable.descriptorSet != VK_NULL_HANDLE) {
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            renderable.pipeline.layout, 1 /* firstSet */, 1,
                            &renderable.descriptorSet, 0, nullptr);
  }

  if (renderable.vertexBuffer.buffer != VK_NULL_HANDLE) {
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &renderable.vertexBuffer.buffer,
                           &renderable.vertexBufferBindingOffset);
  }

  if (renderable.indexBuffer.buffer != VK_NULL_HANDLE) {
    vkCmdBindIndexBuffer(commandBuffer, renderable.indexBuffer.buffer,
                         renderable.indexBufferBindingOffset,
                         renderable.indexType);
  }

  if (renderable.numIndices > 0) {
    vkCmdDrawIndexed(commandBuffer, renderable.numIndices,
                     renderable.instanceCount, renderable.firstIndex,
                     renderable.vertexOffset, renderable.firstInstance);
  } else {
    vkCmdDraw(commandBuffer, renderable.numVertices, renderable.instanceCount,
              renderable.firstVertex, renderable.firstInstance);
  }

  vkEndCommandBuffer(commandBuffer);
  return commandBuffer;
} // RenderRenderable

static void UpdateUIRenderable(Component::Renderable& renderable,
                               ImDrawData* drawData,
                               std::string const& title) noexcept {
  if (auto vb = ReallocateBuffer(
        renderable.vertexBuffer, drawData->TotalVtxCount * sizeof(ImDrawVert),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU)) {
    renderable.vertexBuffer = std::move(*vb);
    NameObject(VK_OBJECT_TYPE_BUFFER, renderable.vertexBuffer.buffer,
               (title + "::uiRenderable.vertexBuffer").c_str());
  } else {
    GetLogger()->warn(
      "Unable to create/resize ui vertex buffer for window {}: {}", title,
      vb.error().what());
    return;
  }

  if (auto ib = ReallocateBuffer(
        renderable.indexBuffer, drawData->TotalIdxCount * sizeof(ImDrawIdx),
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU)) {
    renderable.indexBuffer = std::move(*ib);
    NameObject(VK_OBJECT_TYPE_BUFFER, renderable.indexBuffer.buffer,
               (title + "::uiRenderable.indexBuffer").c_str());
  } else {
    GetLogger()->warn(
      "Unable to create/resize ui index buffer for window {}: {}", title,
      ib.error().what());
    return;
  }

  ImDrawVert* pVertices;
  ImDrawIdx* pIndices;

  if (auto ptr = renderable.vertexBuffer.Map<ImDrawVert*>()) {
    pVertices = std::move(*ptr);
  } else {
    GetLogger()->warn("Unable to map ui vertex buffer for window {}: {}", title,
                      ptr.error().what());
    return;
  }

  if (auto ptr = renderable.indexBuffer.Map<ImDrawIdx*>()) {
    pIndices = std::move(*ptr);
  } else {
    GetLogger()->warn("Unable to map ui index buffer for window {}: {}", title,
                      ptr.error().what());
    return;
  }

  for (int i = 0; i < drawData->CmdListsCount; ++i) {
    ImDrawList const* cmdList = drawData->CmdLists[i];
    std::memcpy(pVertices, cmdList->VtxBuffer.Data,
                cmdList->VtxBuffer.Size * sizeof(ImDrawVert));
    std::memcpy(pIndices, cmdList->IdxBuffer.Data,
                cmdList->IdxBuffer.Size * sizeof(ImDrawIdx));
    pVertices += cmdList->VtxBuffer.Size;
    pIndices += cmdList->IdxBuffer.Size;
  }

  renderable.vertexBuffer.Unmap();
  renderable.indexBuffer.Unmap();
} // UpdateUIRenderable

static VKAPI_ATTR VkBool32 VKAPI_CALL DebugUtilsMessengerCallback(
  VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
  VkDebugUtilsMessageTypeFlagsEXT messageTypes,
  VkDebugUtilsMessengerCallbackDataEXT const* pCallbackData, void*) {
  using namespace std::string_literals;

  fmt::memory_buffer buf;
  fmt::format_to(
    buf, "{}: {}",
    to_string(static_cast<VkDebugUtilsMessageTypeFlagBitsEXT>(messageTypes)),
    pCallbackData->pMessage);
  std::string const msg(buf.data(), buf.size());

  buf.clear();
  for (uint32_t i = 0; i < pCallbackData->objectCount; ++i) {
    if (pCallbackData->pObjects[i].pObjectName) {
      fmt::format_to(buf, "{}, ", pCallbackData->pObjects[i].pObjectName);
    }
  }
  std::string const objNames(buf.data(), buf.size() == 0 ? 0 : buf.size() - 2);

  if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
    if (objNames.empty()) {
      GetLogger()->error(msg);
    } else {
      GetLogger()->error("{} Objects: ({})", msg, objNames);
    }
  } else if (messageSeverity >=
             VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
    if (objNames.empty()) {
      GetLogger()->warn(msg);
    } else {
      GetLogger()->warn("{} Objects: ({})", msg, objNames);
    }
  } else if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
    if (objNames.empty()) {
      GetLogger()->info(msg);
    } else {
      GetLogger()->info("{} Objects: ({})", msg, objNames);
    }
  } else if (messageSeverity >=
             VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) {
    if (objNames.empty()) {
      GetLogger()->trace(msg);
    } else {
      GetLogger()->trace("{} Objects: ({})", msg, objNames);
    }
  }

  GetLogger()->flush();
  return VK_FALSE;
} // DebugUtilsMessengerCallback

} // namespace iris::Renderer

tl::expected<void, std::system_error>
iris::Renderer::Initialize(gsl::czstring<> appName, Options const& options,
                           spdlog::sinks_init_list logSinks,
                           std::uint32_t appVersion) noexcept {
  GetLogger(logSinks);
  Expects(sInstance == VK_NULL_HANDLE);
  IRIS_LOG_ENTER();

  GOOGLE_PROTOBUF_VERIFY_VERSION;
  glslang::InitializeProcess();

  sTaskSchedulerInit.initialize();
  GetLogger()->debug("Default number of task threads: {}",
                     sTaskSchedulerInit.default_num_threads());

#if PLATFORM_LINUX
  ::setenv(
    "VK_LAYER_PATH",
    (iris::kVulkanSDKDirectory + "/etc/vulkan/explicit_layer.d"s).c_str(), 0);
  GetLogger()->debug("VK_LAYER_PATH: {}", ::getenv("VK_LAYER_PATH"));
#endif

  flextVkInit();

  std::uint32_t instanceVersion, versionMajor, versionMinor, versionPatch;
  vkEnumerateInstanceVersion(&instanceVersion); // can only return VK_SUCCESS
  versionMajor = VK_VERSION_MAJOR(instanceVersion);
  versionMinor = VK_VERSION_MINOR(instanceVersion);
  versionPatch = VK_VERSION_PATCH(instanceVersion);

  GetLogger()->debug("Vulkan Instance Version: {}.{}.{}", versionMajor,
                     versionMinor, versionPatch);

  if (versionMajor != 1 && versionMinor != 1) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(Error::kInitializationFailed,
                        fmt::format("Invalid instance version: {}.{}.{}",
                                    versionMajor, versionMinor, versionPatch)));
  }

  // Get the number of instance extension properties.
  std::uint32_t numExtensionProperties;
  if (auto result = vkEnumerateInstanceExtensionProperties(
        nullptr, &numExtensionProperties, nullptr);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(make_error_code(result),
                        "Cannot enumerate instance extension properties"));
  }

  // Get the instance extension properties.
  absl::FixedArray<VkExtensionProperties> extensionProperties(
    numExtensionProperties);
  if (auto result = vkEnumerateInstanceExtensionProperties(
        nullptr, &numExtensionProperties, extensionProperties.data());
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(make_error_code(result),
                        "Cannot enumerate instance extension properties"));
  }

  absl::InlinedVector<gsl::czstring<>, 1> layerNames;
  if ((options & Options::kUseValidationLayers) ==
      Options::kUseValidationLayers) {
    layerNames.push_back("VK_LAYER_LUNARG_standard_validation");
  }

  // These are the extensions that we require from the instance.
  absl::InlinedVector<gsl::czstring<>, 10> instanceExtensionNames = {
    VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
    VK_KHR_SURFACE_EXTENSION_NAME, // surfaces are necessary for graphics
    VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME,
#if defined(VK_USE_PLATFORM_XCB_KHR) // plus the platform-specific surface
    VK_KHR_XCB_SURFACE_EXTENSION_NAME,
#elif defined(VK_USE_PLATFORM_WIN32_KHR) // plus the platform-specific surface
    VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#endif
  };

  if ((options & Options::kReportDebugMessages) ==
      Options::kReportDebugMessages) {
    instanceExtensionNames.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  }

  // These are the features that we require from the physical device.
  VkPhysicalDeviceFeatures2 physicalDeviceFeatures = {};
  physicalDeviceFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  physicalDeviceFeatures.features.fullDrawIndexUint32 = VK_TRUE;
  physicalDeviceFeatures.features.geometryShader = VK_TRUE;
  physicalDeviceFeatures.features.tessellationShader = VK_TRUE;
  physicalDeviceFeatures.features.depthClamp = VK_TRUE;
  physicalDeviceFeatures.features.fillModeNonSolid = VK_TRUE;
  physicalDeviceFeatures.features.wideLines = VK_TRUE;
  physicalDeviceFeatures.features.largePoints = VK_TRUE;
  physicalDeviceFeatures.features.multiViewport = VK_TRUE;
  physicalDeviceFeatures.features.pipelineStatisticsQuery = VK_TRUE;
  physicalDeviceFeatures.features.shaderTessellationAndGeometryPointSize =
    VK_TRUE;
  physicalDeviceFeatures.features.shaderUniformBufferArrayDynamicIndexing =
    VK_TRUE;
  physicalDeviceFeatures.features.shaderSampledImageArrayDynamicIndexing =
    VK_TRUE;
  physicalDeviceFeatures.features.shaderStorageBufferArrayDynamicIndexing =
    VK_TRUE;
  physicalDeviceFeatures.features.shaderStorageImageArrayDynamicIndexing =
    VK_TRUE;
  physicalDeviceFeatures.features.shaderClipDistance = VK_TRUE;
  physicalDeviceFeatures.features.shaderCullDistance = VK_TRUE;
  physicalDeviceFeatures.features.shaderFloat64 = VK_TRUE;
  physicalDeviceFeatures.features.shaderInt64 = VK_TRUE;

  // These are the extensions that we require from the physical device.
  absl::InlinedVector<char const*, 32> physicalDeviceExtensionNames{{
    VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME,
    VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME, // core in 1.1, but necessary for DEDICATED_ALLOCATION
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
  }};

  for (auto&& prop : extensionProperties) {
    if (std::strcmp(prop.extensionName, VK_NV_RAY_TRACING_EXTENSION_NAME)) {
      physicalDeviceExtensionNames.push_back(VK_NV_RAY_TRACING_EXTENSION_NAME);
      sFeatures |= Features::kRayTracing;
    }
  }

  if (auto instance = CreateInstance(
        appName,
        (appVersion == 0
           ? VK_MAKE_VERSION(kVersionMajor, kVersionMinor, kVersionPatch)
           : appVersion),
        instanceExtensionNames, layerNames,
        (options & Options::kReportDebugMessages) ==
            Options::kReportDebugMessages
          ? &DebugUtilsMessengerCallback
          : nullptr)) {
    sInstance = std::move(*instance);
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(instance.error());
  }

  flextVkInitInstance(sInstance); // initialize instance function pointers
  DumpPhysicalDevices(sInstance);

  if ((options & Options::kReportDebugMessages) ==
      Options::kReportDebugMessages) {
    if (auto messenger =
          CreateDebugUtilsMessenger(sInstance, &DebugUtilsMessengerCallback)) {
      sDebugUtilsMessenger = std::move(*messenger);
    } else {
      GetLogger()->warn("Cannot create DebugUtilsMessenger: {}",
                        messenger.error().what());
    }
  }

  if (auto physicalDevice = ChoosePhysicalDevice(
        sInstance, physicalDeviceFeatures, physicalDeviceExtensionNames,
        VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) {
    sPhysicalDevice = std::move(*physicalDevice);
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(physicalDevice.error());
  }

  if (auto qfi = GetQueueFamilyIndex(sPhysicalDevice, VK_QUEUE_GRAPHICS_BIT |
                                                        VK_QUEUE_COMPUTE_BIT)) {
    sQueueFamilyIndex = *qfi;
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(qfi.error());
  }

  std::uint32_t numQueues;
  if (auto dn = CreateDevice(sPhysicalDevice, physicalDeviceFeatures,
                             physicalDeviceExtensionNames, sQueueFamilyIndex)) {
    std::tie(sDevice, numQueues) = *dn;
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(dn.error());
  }

  NameObject(VK_OBJECT_TYPE_INSTANCE, sInstance, "sInstance");
  NameObject(VK_OBJECT_TYPE_PHYSICAL_DEVICE, sPhysicalDevice,
             "sPhysicalDevice");
  NameObject(VK_OBJECT_TYPE_DEVICE, sDevice, "sDevice");

  sCommandQueues.resize(numQueues);
  sCommandPools.resize(numQueues);
  sCommandFences.resize(numQueues);

  VkCommandPoolCreateInfo commandPoolCI = {};
  commandPoolCI.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  commandPoolCI.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  commandPoolCI.queueFamilyIndex = sQueueFamilyIndex;

  VkFenceCreateInfo fenceCI = {};
  fenceCI.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

  for (std::uint32_t i = 0; i < numQueues; ++i) {
    vkGetDeviceQueue(sDevice, sQueueFamilyIndex, i, &sCommandQueues[i]);

    NameObject(VK_OBJECT_TYPE_QUEUE, sCommandQueues[i],
               fmt::format("sCommandQueue[{}]", i).c_str());

    if (auto result = vkCreateCommandPool(sDevice, &commandPoolCI, nullptr,
                                          &sCommandPools[i]);
        result != VK_SUCCESS) {
      IRIS_LOG_LEAVE();
      return tl::unexpected(std::system_error(
        make_error_code(result), "Cannot create graphics command pool"));
    }

    NameObject(VK_OBJECT_TYPE_COMMAND_POOL, &sCommandPools[i],
               fmt::format("sCommandPools[{}]", i).c_str());

    if (auto result =
          vkCreateFence(sDevice, &fenceCI, nullptr, &sCommandFences[i]);
        result != VK_SUCCESS) {
      IRIS_LOG_LEAVE();
      return tl::unexpected(std::system_error(
        make_error_code(result), "Cannot create graphics submit fence"));
    }

    NameObject(VK_OBJECT_TYPE_FENCE, &sCommandFences[i],
               fmt::format("sCommandFences[{}]", i).c_str());
  }

  if (auto allocator = CreateAllocator(sPhysicalDevice, sDevice)) {
    sAllocator = std::move(*allocator);
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(allocator.error());
  }

  /////
  //
  // Create the RenderPass
  //
  ////

  absl::FixedArray<VkAttachmentDescription> attachments(
    sNumRenderPassAttachments);
  attachments[sColorTargetAttachmentIndex] = VkAttachmentDescription{
    0,                                       // flags
    sSurfaceColorFormat.format,              // format
    sSurfaceSampleCount,                     // samples
    VK_ATTACHMENT_LOAD_OP_CLEAR,             // loadOp (color and depth)
    VK_ATTACHMENT_STORE_OP_DONT_CARE,        // storeOp (color and depth)
    VK_ATTACHMENT_LOAD_OP_DONT_CARE,         // stencilLoadOp
    VK_ATTACHMENT_STORE_OP_DONT_CARE,        // stencilStoreOp
    VK_IMAGE_LAYOUT_UNDEFINED,               // initialLayout
    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL // finalLayout
  };

  attachments[sColorResolveAttachmentIndex] = VkAttachmentDescription{
    0,                                // flags
    sSurfaceColorFormat.format,       // format
    VK_SAMPLE_COUNT_1_BIT,            // samples
    VK_ATTACHMENT_LOAD_OP_DONT_CARE,  // loadOp (color and depth)
    VK_ATTACHMENT_STORE_OP_STORE,     // storeOp (color and depth)
    VK_ATTACHMENT_LOAD_OP_DONT_CARE,  // stencilLoadOp
    VK_ATTACHMENT_STORE_OP_DONT_CARE, // stencilStoreOp
    VK_IMAGE_LAYOUT_UNDEFINED,        // initialLayout
    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR   // finalLayout
  };

  attachments[sDepthStencilTargetAttachmentIndex] = VkAttachmentDescription{
    0,                                // flags
    sSurfaceDepthStencilFormat,       // format
    sSurfaceSampleCount,              // samples
    VK_ATTACHMENT_LOAD_OP_CLEAR,      // loadOp (color and depth)
    VK_ATTACHMENT_STORE_OP_DONT_CARE, // storeOp (color and depth)
    VK_ATTACHMENT_LOAD_OP_CLEAR,      // stencilLoadOp
    VK_ATTACHMENT_STORE_OP_DONT_CARE, // stencilStoreOp
    VK_IMAGE_LAYOUT_UNDEFINED,        // initialLayout
    VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL // finalLayout
  };

  attachments[sDepthStencilResolveAttachmentIndex] = VkAttachmentDescription{
    0,                               // flags
    sSurfaceDepthStencilFormat,      // format
    VK_SAMPLE_COUNT_1_BIT,           // samples
    VK_ATTACHMENT_LOAD_OP_DONT_CARE, // loadOp (color and depth)
    VK_ATTACHMENT_STORE_OP_STORE,    // storeOp (color and depth)
    VK_ATTACHMENT_LOAD_OP_DONT_CARE, // stencilLoadOp
    VK_ATTACHMENT_STORE_OP_STORE,    // stencilStoreOp
    VK_IMAGE_LAYOUT_UNDEFINED,       // initialLayout
    VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL // finalLayout
  };

  VkAttachmentReference color{sColorTargetAttachmentIndex,
                              VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkAttachmentReference resolve{sColorResolveAttachmentIndex,
                                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkAttachmentReference depthStencil{
    sDepthStencilTargetAttachmentIndex,
    VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

  VkSubpassDescription subpass = {
    0,                               // flags
    VK_PIPELINE_BIND_POINT_GRAPHICS, // pipelineBindPoint
    0,                               // inputAttachmentCount
    nullptr,                         // pInputAttachments
    1,                               // colorAttachmentCount
    &color,                          // pColorAttachments (array)
    &resolve,                        // pResolveAttachments (array)
    &depthStencil,                   // pDepthStencilAttachment (single)
    0,                               // preserveAttachmentCount
    nullptr                          // pPreserveAttachments
  };

  VkRenderPassCreateInfo renderPassCI = {};
  renderPassCI.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  renderPassCI.attachmentCount = attachments.size();
  renderPassCI.pAttachments = attachments.data();
  renderPassCI.subpassCount = 1;
  renderPassCI.pSubpasses = &subpass;

  if (auto result =
        vkCreateRenderPass(sDevice, &renderPassCI, nullptr, &sRenderPass);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(make_error_code(result), "Cannot create render pass"));
  }

  NameObject(VK_OBJECT_TYPE_RENDER_PASS, sRenderPass, "sRenderPass");

  for (auto&& fence : sFrameFinishedFences) {
    if (auto result = vkCreateFence(sDevice, &fenceCI, nullptr, &fence);
        result != VK_SUCCESS) {
      IRIS_LOG_LEAVE();
      return tl::unexpected(std::system_error(
        make_error_code(result), "Cannot create frame finished fence"));
    }
  }

  VkSemaphoreCreateInfo semaphoreCI = {};
  semaphoreCI.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

  if (auto result = vkCreateSemaphore(sDevice, &semaphoreCI, nullptr,
                                      &sImagesReadyForPresent);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(
      make_error_code(result), "Cannot create images ready semaphore"));
  }

  absl::FixedArray<VkDescriptorPoolSize> poolSizes{
    {VK_DESCRIPTOR_TYPE_SAMPLER, 128},
    {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 128},
    {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 128},
    {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 128},
    {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 128},
    {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 128},
    {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 128},
    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 128},
    {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 128},
    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 128},
    {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 128},
    {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV, 128},
  };

  VkDescriptorPoolCreateInfo descriptorPoolCI = {};
  descriptorPoolCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  descriptorPoolCI.maxSets = 128;
  descriptorPoolCI.poolSizeCount =
    gsl::narrow_cast<std::uint32_t>(poolSizes.size());
  descriptorPoolCI.pPoolSizes = poolSizes.data();

  if (auto result = vkCreateDescriptorPool(sDevice, &descriptorPoolCI, nullptr,
                                           &sDescriptorPool);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(make_error_code(result),
                                            "Cannot create descriptor pool"));
  }

  NameObject(VK_OBJECT_TYPE_DESCRIPTOR_POOL, sDescriptorPool,
             "sDescriptorPool");

  VkQueryPoolCreateInfo queryPoolCI = {};
  queryPoolCI.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
  queryPoolCI.queryType = VK_QUERY_TYPE_TIMESTAMP;
  queryPoolCI.queryCount = 128;

  if (auto result = vkCreateQueryPool(sDevice, &queryPoolCI, nullptr,
                                      &sTimestampsQueryPool);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(
      make_error_code(result), "Cannot create timestamps query pool"));
  }

  /////
  //
  // Create the global descriptor set for all pipelines
  //
  /////

  absl::FixedArray<VkDescriptorSetLayoutBinding> descriptorSetLayoutBindings{
    // This is the MatricesBuffer
    {
      0,                                 // binding
      VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, // descriptorType
      1,                                 // descriptorCount
      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT |
        VK_SHADER_STAGE_RAYGEN_BIT_NV |
        VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV, // stageFlags
      nullptr                               // pImmutableSamplers
    },
    // This is the LightsBuffer
    {
      1,                                 // binding
      VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, // descriptorType
      1,                                 // descriptorCount
      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT |
        VK_SHADER_STAGE_RAYGEN_BIT_NV |
        VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV, // stageFlags
      nullptr                               // pImmutableSamplers
    }};

  VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI = {};
  descriptorSetLayoutCI.sType =
    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  descriptorSetLayoutCI.bindingCount =
    gsl::narrow_cast<std::uint32_t>(descriptorSetLayoutBindings.size());
  descriptorSetLayoutCI.pBindings = descriptorSetLayoutBindings.data();

  if (auto result = vkCreateDescriptorSetLayout(
        sDevice, &descriptorSetLayoutCI, nullptr, &sGlobalDescriptorSetLayout);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(
      make_error_code(result), "Cannot create descriptor set layout"));
  }

  NameObject(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, sGlobalDescriptorSetLayout,
             "sGlobalDescriptorSetLayout");

  VkDescriptorSetAllocateInfo descriptorSetAI = {};
  descriptorSetAI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  descriptorSetAI.descriptorPool = sDescriptorPool;
  descriptorSetAI.descriptorSetCount = 1;
  descriptorSetAI.pSetLayouts = &sGlobalDescriptorSetLayout;

  if (auto result = vkAllocateDescriptorSets(sDevice, &descriptorSetAI,
                                             &sGlobalDescriptorSet);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(make_error_code(result),
                                            "Cannot allocate descriptor set"));
  }

  NameObject(VK_OBJECT_TYPE_DESCRIPTOR_SET, sGlobalDescriptorSet,
             "sGlobalDescriptorSet");

  if (auto buf = AllocateBuffer(sizeof(MatricesBuffer),
                                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                VMA_MEMORY_USAGE_CPU_TO_GPU)) {
    sMatricesBuffer = std::move(*buf);
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(buf.error());
  }

  NameObject(VK_OBJECT_TYPE_BUFFER, sMatricesBuffer.buffer, "sMatricesBuffer");

  if (auto buf = AllocateBuffer(sizeof(LightsBuffer),
                                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                VMA_MEMORY_USAGE_CPU_TO_GPU)) {
    sLightsBuffer = std::move(*buf);
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(buf.error());
  }

  NameObject(VK_OBJECT_TYPE_BUFFER, sLightsBuffer.buffer, "sLightsBuffer");

  VkDescriptorBufferInfo matricesBufferInfo = {};
  matricesBufferInfo.buffer = sMatricesBuffer.buffer;
  matricesBufferInfo.offset = 0;
  matricesBufferInfo.range = VK_WHOLE_SIZE;

  VkDescriptorBufferInfo lightsBufferInfo = {};
  lightsBufferInfo.buffer = sLightsBuffer.buffer;
  lightsBufferInfo.offset = 0;
  lightsBufferInfo.range = VK_WHOLE_SIZE;

  absl::FixedArray<VkWriteDescriptorSet> writeDescriptorSets{
    {
      VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
      sGlobalDescriptorSet,              // dstSet
      0,                                 // dstBinding
      0,                                 // dstArrayElement
      1,                                 // descriptorCount
      VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, // descriptorType
      nullptr,                           // pImageInfo
      &matricesBufferInfo,               // pBufferInfo
      nullptr                            // pTexelBufferView
    },
    {
      VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
      sGlobalDescriptorSet,              // dstSet
      1,                                 // setBinding
      0,                                 // dstArrayElement
      1,                                 // descriptorCount
      VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, // descriptorType
      nullptr,                           // pImageInfo
      &lightsBufferInfo,                 // pBufferInfo
      nullptr                            // pTexelBufferView
    }};

  vkUpdateDescriptorSets(
    sDevice, gsl::narrow_cast<std::uint32_t>(writeDescriptorSets.size()),
    writeDescriptorSets.data(), 0, nullptr);

  /////
  //
  // Create UI Pipeline and other shared state
  //
  /////

  absl::FixedArray<VkDescriptorSetLayoutBinding> uiDescriptorSetLayoutBindings{
    {
      0,                            // binding
      VK_DESCRIPTOR_TYPE_SAMPLER,   // descriptorType
      1,                            // descriptorCount
      VK_SHADER_STAGE_FRAGMENT_BIT, // stageFlags
      nullptr                       // pImmutableSamplers
    },
    {
      1,                                // binding
      VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, // descriptorType
      1,                                // descriptorCount
      VK_SHADER_STAGE_FRAGMENT_BIT,     // stageFlags
      nullptr                           // pImmutableSamplers
    }};

  VkDescriptorSetLayoutCreateInfo uiDescriptorSetLayoutCI = {};
  uiDescriptorSetLayoutCI.sType =
    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  uiDescriptorSetLayoutCI.bindingCount =
    gsl::narrow_cast<std::uint32_t>(uiDescriptorSetLayoutBindings.size());
  uiDescriptorSetLayoutCI.pBindings = uiDescriptorSetLayoutBindings.data();

  if (auto result = vkCreateDescriptorSetLayout(
        sDevice, &uiDescriptorSetLayoutCI, nullptr, &sUIDescriptorSetLayout);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(
      make_error_code(result), "Cannot create UI descriptor set layout"));
  }

  NameObject(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, sUIDescriptorSetLayout,
             "sUIDescriptorSetLayout");

  VkDescriptorSetAllocateInfo uiDescriptorSetAI = {};
  uiDescriptorSetAI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  uiDescriptorSetAI.descriptorPool = sDescriptorPool;
  uiDescriptorSetAI.descriptorSetCount = 1;
  uiDescriptorSetAI.pSetLayouts = &sUIDescriptorSetLayout;

  if (auto result = vkAllocateDescriptorSets(sDevice, &uiDescriptorSetAI,
                                             &sUIDescriptorSet);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(
      make_error_code(result), "Cannot allocate UI descriptor set"));
  }

  NameObject(VK_OBJECT_TYPE_DESCRIPTOR_SET, sUIDescriptorSet,
             "sUIDescriptorSet");

  absl::FixedArray<Shader> shaders(2);

  if (auto vs = CompileShaderFromSource(sUIVertexShaderSource,
                                        VK_SHADER_STAGE_VERTEX_BIT)) {
    shaders[0] = std::move(*vs);
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(vs.error());
  }

  if (auto fs = CompileShaderFromSource(sUIFragmentShaderSource,
                                        VK_SHADER_STAGE_FRAGMENT_BIT)) {
    shaders[1] = std::move(*fs);
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(fs.error());
  }

  absl::FixedArray<VkPushConstantRange> pushConstantRanges{
    {VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::vec2) * 2}};

  absl::FixedArray<VkVertexInputBindingDescription>
    vertexInputBindingDescriptions(1);
  vertexInputBindingDescriptions[0] = {0, sizeof(ImDrawVert),
                                       VK_VERTEX_INPUT_RATE_VERTEX};

  absl::FixedArray<VkVertexInputAttributeDescription>
    vertexInputAttributeDescriptions(3);
  vertexInputAttributeDescriptions[0] = {0, 0, VK_FORMAT_R32G32_SFLOAT,
                                         offsetof(ImDrawVert, pos)};
  vertexInputAttributeDescriptions[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT,
                                         offsetof(ImDrawVert, uv)};
  vertexInputAttributeDescriptions[2] = {2, 0, VK_FORMAT_R8G8B8A8_UNORM,
                                         offsetof(ImDrawVert, col)};

  VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI = {};
  inputAssemblyStateCI.sType =
    VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  inputAssemblyStateCI.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  VkPipelineViewportStateCreateInfo viewportStateCI = {};
  viewportStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewportStateCI.viewportCount = 1;
  viewportStateCI.scissorCount = 1;

  VkPipelineRasterizationStateCreateInfo rasterizationStateCI = {};
  rasterizationStateCI.sType =
    VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterizationStateCI.polygonMode = VK_POLYGON_MODE_FILL;
  rasterizationStateCI.cullMode = VK_CULL_MODE_FRONT_BIT;
  rasterizationStateCI.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rasterizationStateCI.lineWidth = 1.f;

  VkPipelineMultisampleStateCreateInfo multisampleStateCI = {};
  multisampleStateCI.sType =
    VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisampleStateCI.rasterizationSamples = sSurfaceSampleCount;
  multisampleStateCI.minSampleShading = 1.f;

  VkPipelineDepthStencilStateCreateInfo depthStencilStateCI = {};
  depthStencilStateCI.sType =
    VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;

  absl::FixedArray<VkPipelineColorBlendAttachmentState>
    colorBlendAttachmentStates(1);
  colorBlendAttachmentStates[0] = {
    VK_TRUE,                             // blendEnable
    VK_BLEND_FACTOR_SRC_ALPHA,           // srcColorBlendFactor
    VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, // dstColorBlendFactor
    VK_BLEND_OP_ADD,                     // colorBlendOp
    VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, // srcAlphaBlendFactor
    VK_BLEND_FACTOR_ZERO,                // dstAlphaBlendFactor
    VK_BLEND_OP_ADD,                     // alphaBlendOp
    VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT // colorWriteMask
  };

  absl::FixedArray<VkDynamicState> dynamicStates{VK_DYNAMIC_STATE_VIEWPORT,
                                                 VK_DYNAMIC_STATE_SCISSOR};

  if (auto pipe = CreateRasterizationPipeline(
        shaders, vertexInputBindingDescriptions,
        vertexInputAttributeDescriptions, inputAssemblyStateCI, viewportStateCI,
        rasterizationStateCI, multisampleStateCI, depthStencilStateCI,
        colorBlendAttachmentStates, dynamicStates, 0,
        gsl::make_span(&sUIDescriptorSetLayout, 1))) {
    sUIPipeline = std::move(*pipe);
  } else {
    using namespace std::string_literals;
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(pipe.error().code(),
                        "Cannot create UI pipeline: "s + pipe.error().what()));
  }

  NameObject(VK_OBJECT_TYPE_PIPELINE_LAYOUT, sUIPipeline.layout,
             "sUIPipeline.layout");
  NameObject(VK_OBJECT_TYPE_PIPELINE, sUIPipeline.pipeline,
             "sUIPipeline.pipeline");

  sRunning = true;
  IRIS_LOG_LEAVE();
  return {};
} // iris::Renderer::Create

iris::Renderer::Features iris::Renderer::AvailableFeatures() noexcept {
  Expects(sInstance != VK_NULL_HANDLE);
  return sFeatures;
}

bool iris::Renderer::IsRunning() noexcept {
  return sRunning;
} // iris::Renderer::IsRunning

void iris::Renderer::Terminate() noexcept {
  IRIS_LOG_ENTER();
  sRunning = false;
  IRIS_LOG_LEAVE();
} // iris::Renderer::Terminate

VkRenderPass iris::Renderer::BeginFrame() noexcept {
  Expects(sRunning);
  Expects(!sInFrame);

  auto const currentTime = std::chrono::steady_clock::now();
  std::chrono::duration<float> const delta = currentTime - sPreviousFrameTime;
  sPreviousFrameTime = currentTime;

  decltype(sIOContinuations)::value_type ioContinuation;
  while (sIOContinuations.try_pop(ioContinuation)) {
    if (auto error = ioContinuation(); error.code()) {
      GetLogger()->error(error.what());
    }
  }

  auto&& windows = Windows();

  for (auto&& [title, window] : windows) {
    ImGui::SetCurrentContext(window.uiContext.get());

    window.platformWindow.PollEvents();
    if (ImGui::IsKeyReleased(wsi::Keys::kEscape)) Terminate();

    if (window.resized) {
      auto const newExtent = window.platformWindow.Extent();
      if (auto result =
            ResizeWindow(window, {newExtent.width, newExtent.height});
          !result) {
        GetLogger()->error("Error resizing window {}: {}", title,
                           result.error().what());
      } else {
        window.resized = false;
      }
    }

    ImGuiIO& io = ImGui::GetIO();

    io.DisplaySize = {static_cast<float>(window.extent.width),
                      static_cast<float>(window.extent.height)};
    io.DeltaTime = delta.count();

    io.KeyCtrl = ImGui::IsKeyDown(wsi::Keys::kLeftControl) |
                 ImGui::IsKeyDown(wsi::Keys::kRightControl);
    io.KeyShift = ImGui::IsKeyDown(wsi::Keys::kLeftShift) |
                  ImGui::IsKeyDown(wsi::Keys::kRightShift);
    io.KeyAlt = ImGui::IsKeyDown(wsi::Keys::kLeftAlt) |
                ImGui::IsKeyDown(wsi::Keys::kRightAlt);
    io.KeySuper = ImGui::IsKeyDown(wsi::Keys::kLeftSuper) |
                  ImGui::IsKeyDown(wsi::Keys::kRightSuper);

    io.MousePos = window.platformWindow.CursorPos();

    // TODO: update mouse cursor based on imgui request

    ImGui::NewFrame();
  }

  if (sFrameNum != 0) {
    VkFence frameFinishedFence =
      sFrameFinishedFences[(sFrameIndex - 1) % sNumWindowFramesBuffered];

    if (auto result =
          vkWaitForFences(sDevice, 1, &frameFinishedFence, VK_TRUE, UINT64_MAX);
        result != VK_SUCCESS) {
      GetLogger()->error("Error waiting for frame finished fence: {}",
                         make_error_code(result).message());
    }

    if (auto result = vkResetFences(sDevice, 1, &frameFinishedFence);
        result != VK_SUCCESS) {
      GetLogger()->error("Error resetting frame finished fence: {}",
                         make_error_code(result).message());
    }
  }

  sInFrame = true;
  return sRenderPass;
} // iris::Renderer::BeginFrame()

void iris::Renderer::BindDescriptorSets(
  VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint,
  VkPipelineLayout layout, gsl::span<VkDescriptorSet> descriptorSets) noexcept {

  vkCmdBindDescriptorSets(commandBuffer,         // commandBuffer
                          pipelineBindPoint,     // pipelineBindPoint
                          layout,                // layout
                          0,                     // firstSet
                          1,                     // descriptorSetCount
                          &sGlobalDescriptorSet, // pDescriptorSets
                          0,                     // dynamicOffsetCount
                          nullptr                // pDynamicOffsets
  );

  vkCmdBindDescriptorSets(commandBuffer,         // commandBuffer
                          pipelineBindPoint,     // pipelineBindPoint
                          layout,                // layout
                          1,                     // firstSet
                          descriptorSets.size(), // descriptorSetCount
                          descriptorSets.data(), // pDescriptorSets
                          0,                     // dynamicOffsetCount
                          nullptr                // pDynamicOffsets
  );
} // iris::Renderer::BindDescriptorSets

void iris::Renderer::EndFrame(VkImage image,
  gsl::span<const VkCommandBuffer> secondaryCBs) noexcept {
  Expects(sInFrame);

  auto&& windows = Windows();
  std::size_t const numWindows = windows.size();

  VkResult result;

  absl::FixedArray<VkSemaphore> waitSemaphores(numWindows);
  absl::FixedArray<VkSwapchainKHR> swapchains(numWindows);
  absl::FixedArray<std::uint32_t> imageIndices(numWindows);
  absl::FixedArray<VkCommandBuffer> commandBuffers(numWindows);

  absl::FixedArray<VkClearValue> clearValues(sNumRenderPassAttachments);
  clearValues[sDepthStencilTargetAttachmentIndex].depthStencil = {1.f, 0};

  VkCommandBufferBeginInfo commandBufferBI = {};
  commandBufferBI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  commandBufferBI.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

  VkRenderPassBeginInfo renderPassBI = {};
  renderPassBI.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  renderPassBI.renderPass = sRenderPass;
  renderPassBI.clearValueCount = clearValues.size();

  for (auto&& [i, iter] : enumerate(windows)) {
    auto&& [title, window] = iter;
    ImGui::SetCurrentContext(window.uiContext.get());

    // TODO: how to handle this at application scope?
    if (window.showUI) {
      ImGui::Begin("Status");
      ImGui::Text("Last Frame %.3f ms", ImGui::GetIO().DeltaTime);
      ImGui::Text("Average %.3f ms/frame (%.1f FPS)",
                  1000.f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
      ImGui::End();
    }

    ImGui::EndFrame();

    // currentFrame is still the previous frame, use that imageAvailable
    // semaphore. vkAcquireNextImageKHR will update frameIndex thereby updating
    // currentFrame (via frameIndex).
    window.imageAcquired = window.currentFrame().imageAvailable;

    if (result = vkAcquireNextImageKHR(sDevice, window.swapchain, UINT64_MAX,
                                       window.imageAcquired, VK_NULL_HANDLE,
                                       &window.frameIndex);
        result == VK_SUBOPTIMAL_KHR || result == VK_ERROR_OUT_OF_DATE_KHR) {
      GetLogger()->warn("Window {} swapchain out of date: resizing", title);
      auto const newExtent = window.platformWindow.Extent();

      if (auto r = ResizeWindow(window, {newExtent.width, newExtent.height});
          !r) {
        GetLogger()->error("Error resizing window {}: {}", title,
                           r.error().what());
      }

      result = vkAcquireNextImageKHR(sDevice, window.swapchain, UINT64_MAX,
                                     window.imageAcquired, VK_NULL_HANDLE,
                                     &window.frameIndex);
    }

    if (result != VK_SUCCESS) {
      GetLogger()->error("Error acquiring next image for window {}: {}", title,
                         make_error_code(result).message());
    }

    // FIXME: move this
    glm::mat4 const viewMatrix = glm::lookAt(
      glm::vec3(1.f, 1.f, -1.f), glm::vec3(0.f), glm::vec3(0.f, 1.f, 0.f));

    if (auto ptr = sMatricesBuffer.Map<MatricesBuffer*>()) {
      (*ptr)->ViewMatrix = viewMatrix;
      (*ptr)->ViewMatrixInverse = glm::inverse(viewMatrix);
      (*ptr)->ProjectionMatrix = window.projectionMatrix;
      (*ptr)->ProjectionMatrixInverse = window.projectionMatrixInverse;
      sMatricesBuffer.Unmap();
    } else {
      GetLogger()->error("Cannot update matrices buffer: {}",
                         ptr.error().what());
    }

    Window::Frame& frame = window.currentFrame();

    if (result = vkResetCommandPool(sDevice, frame.commandPool, 0);
        result != VK_SUCCESS) {
      GetLogger()->error("Error resetting window {} frame {} command pool: {}",
                         title, window.frameIndex,
                         make_error_code(result).message());
    }

    if (result = vkBeginCommandBuffer(frame.commandBuffer, &commandBufferBI);
        result != VK_SUCCESS) {
      GetLogger()->error(
        "Error beginning window {} frame {} command buffer: {}", title,
        window.frameIndex, make_error_code(result).message());
    }

    vkCmdSetViewport(frame.commandBuffer, 0, 1, &window.viewport);
    vkCmdSetScissor(frame.commandBuffer, 0, 1, &window.scissor);

    PushConstants pushConstants;
    pushConstants.iMouse = {window.lastMousePos.x, window.lastMousePos.y, 0.f,
                            0.f};

    if (ImGui::IsMouseDown(iris::wsi::Buttons::kButtonLeft)) {
      window.lastMousePos.x = ImGui::GetIO().MousePos.x;
      window.lastMousePos.y = ImGui::GetIO().MousePos.y;
    }
    if (ImGui::IsMouseReleased(iris::wsi::Buttons::kButtonLeft)) {
      pushConstants.iMouse.z = ImGui::GetIO().MousePos.x;
      pushConstants.iMouse.w = ImGui::GetIO().MousePos.y;
    }

    pushConstants.iTimeDelta = ImGui::GetIO().DeltaTime;
    pushConstants.iTime = ImGui::GetTime();
    pushConstants.iFrame = ImGui::GetFrameCount();
    pushConstants.iFrameRate = pushConstants.iFrame / pushConstants.iTime;
    pushConstants.iResolution.x = ImGui::GetIO().DisplaySize.x;
    pushConstants.iResolution.y = ImGui::GetIO().DisplaySize.y;
    pushConstants.iResolution.z =
      pushConstants.iResolution.x / pushConstants.iResolution.y;

    if (image != VK_NULL_HANDLE) {
      VkCommandBuffer commandBuffer =
        CopyImage(window.colorImages[window.frameIndex], image,
                  {window.extent.width, window.extent.height, 1});
      vkCmdExecuteCommands(frame.commandBuffer, 1, &commandBuffer);
    }

    clearValues[sColorTargetAttachmentIndex].color = window.clearColor;

    renderPassBI.framebuffer = frame.framebuffer;
    renderPassBI.renderArea.extent = window.extent;
    renderPassBI.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(frame.commandBuffer, &renderPassBI,
                         VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);

    for (auto&& renderable : sRenderables()) {
      pushConstants.ModelMatrix = renderable.modelMatrix;
      pushConstants.ModelViewMatrix = viewMatrix * renderable.modelMatrix;
      pushConstants.ModelViewMatrixInverse =
        glm::inverse(pushConstants.ModelViewMatrix);
      //pushConstants.NormalMatrix =
        //glm::transpose(glm::inverse(glm::mat3(renderable.modelMatrix)));

      VkCommandBuffer commandBuffer = RenderRenderable(
        renderable, &window.viewport, &window.scissor,
        gsl::make_span<std::byte>(reinterpret_cast<std::byte*>(&pushConstants),
                                  sizeof(PushConstants)));
      vkCmdExecuteCommands(frame.commandBuffer, 1, &commandBuffer);
    }

    if (!secondaryCBs.empty()) {
      vkCmdExecuteCommands(frame.commandBuffer,
                           gsl::narrow_cast<std::uint32_t>(secondaryCBs.size()),
                           secondaryCBs.data());
    }

    if (window.showUI) {
      ImGui::Render();
      ImDrawData* drawData = ImGui::GetDrawData();

      if (drawData && drawData->TotalVtxCount > 0) {
        UpdateUIRenderable(window.uiRenderable, drawData, title);

        // TODO: update uiRenderable uniform buffer

        VkDescriptorImageInfo uiSamplerInfo = {};
        uiSamplerInfo.sampler = window.uiRenderable.textureSamplers[0];

        VkDescriptorImageInfo uiTextureInfo = {};
        uiTextureInfo.imageView = window.uiRenderable.textureViews[0];
        uiTextureInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        absl::FixedArray<VkWriteDescriptorSet> uiWriteDescriptorSets(2);
        uiWriteDescriptorSets[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                    nullptr,
                                    sUIDescriptorSet,
                                    0,
                                    0,
                                    1,
                                    VK_DESCRIPTOR_TYPE_SAMPLER,
                                    &uiSamplerInfo,
                                    nullptr,
                                    nullptr};
        uiWriteDescriptorSets[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                    nullptr,
                                    sUIDescriptorSet,
                                    1,
                                    0,
                                    1,
                                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                                    &uiTextureInfo,
                                    nullptr,
                                    nullptr};

        vkUpdateDescriptorSets(
          sDevice,
          gsl::narrow_cast<std::uint32_t>(uiWriteDescriptorSets.size()),
          uiWriteDescriptorSets.data(), 0, nullptr);

        glm::vec2 const displaySize = drawData->DisplaySize;
        glm::vec2 const displayPos = drawData->DisplayPos;

        VkViewport viewport = {0.f,           0.f, displaySize.x,
                               displaySize.y, 0.f, 1.f};

        absl::FixedArray<glm::vec2> uiPushConstants(2);
        uiPushConstants[0] = glm::vec2(2.f, 2.f) / displaySize;
        uiPushConstants[1] =
          glm::vec2(-1.f, -1.f) - displayPos * uiPushConstants[0];

        for (int j = 0, idOff = 0, vtOff = 0; j < drawData->CmdListsCount;
             ++j) {
          ImDrawList* cmdList = drawData->CmdLists[j];

          for (int k = 0; k < cmdList->CmdBuffer.size(); ++k) {
            ImDrawCmd const* drawCmd = &cmdList->CmdBuffer[k];

            VkRect2D scissor;
            scissor.offset.x = (int32_t)(drawCmd->ClipRect.x - displayPos.x) > 0
                                 ? (int32_t)(drawCmd->ClipRect.x - displayPos.x)
                                 : 0;
            scissor.offset.y = (int32_t)(drawCmd->ClipRect.y - displayPos.y) > 0
                                 ? (int32_t)(drawCmd->ClipRect.y - displayPos.y)
                                 : 0;
            scissor.extent.width =
              (uint32_t)(drawCmd->ClipRect.z - drawCmd->ClipRect.x);
            scissor.extent.height =
              (uint32_t)(drawCmd->ClipRect.w - drawCmd->ClipRect.y + 1);
            // TODO: why + 1 above?

            // TODO: this has to change: maybe not use Renderable for UI?
            Component::Renderable renderable = window.uiRenderable;
            renderable.pipeline = sUIPipeline;
            renderable.descriptorSet = sUIDescriptorSet;
            renderable.indexType = VK_INDEX_TYPE_UINT16;
            renderable.numIndices = drawCmd->ElemCount;
            renderable.firstIndex = idOff;
            renderable.firstVertex = vtOff;

            VkCommandBuffer commandBuffer = RenderRenderable(
              renderable, &viewport, &scissor,
              gsl::make_span<std::byte>(
                reinterpret_cast<std::byte*>(uiPushConstants.data()),
                uiPushConstants.size() * sizeof(glm::vec2)));
            vkCmdExecuteCommands(frame.commandBuffer, 1, &commandBuffer);

            idOff += drawCmd->ElemCount;
          }

          vtOff += cmdList->VtxBuffer.Size;
        }
      }
    }

    vkCmdEndRenderPass(frame.commandBuffer);
    if (result = vkEndCommandBuffer(frame.commandBuffer);
        result != VK_SUCCESS) {
      GetLogger()->error("Error ending window {} frame {} command buffer: {}",
                         title, window.frameIndex,
                         make_error_code(result).message());
    }

    waitSemaphores[i] = window.imageAcquired;
    swapchains[i] = window.swapchain;
    imageIndices[i] = window.frameIndex;
    commandBuffers[i] = frame.commandBuffer;
  }

  absl::FixedArray<VkPipelineStageFlags> waitDstStages(
    numWindows, VK_PIPELINE_STAGE_TRANSFER_BIT);

  VkSubmitInfo submitI = {};
  submitI.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitI.waitSemaphoreCount = gsl::narrow_cast<std::uint32_t>(numWindows);
  submitI.pWaitSemaphores = waitSemaphores.data();
  submitI.pWaitDstStageMask = waitDstStages.data();
  submitI.commandBufferCount =
    gsl::narrow_cast<std::uint32_t>(commandBuffers.size());
  submitI.pCommandBuffers = commandBuffers.data();

  if (!swapchains.empty()) {
    submitI.signalSemaphoreCount = 1;
    submitI.pSignalSemaphores = &sImagesReadyForPresent;
  }

  VkFence frameFinishedFence = sFrameFinishedFences[sFrameIndex];

  if (result =
        vkQueueSubmit(sCommandQueues[0], 1, &submitI, frameFinishedFence);
      result != VK_SUCCESS) {
    GetLogger()->error("Error submitting command buffer: {}",
                       iris::to_string(result));
  }

  if (!swapchains.empty()) {
    absl::FixedArray<VkResult> presentResults(numWindows);

    VkPresentInfoKHR presentI = {};
    presentI.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentI.waitSemaphoreCount = 1;
    presentI.pWaitSemaphores = &sImagesReadyForPresent;
    presentI.swapchainCount = gsl::narrow_cast<std::uint32_t>(numWindows);
    presentI.pSwapchains = swapchains.data();
    presentI.pImageIndices = imageIndices.data();
    presentI.pResults = presentResults.data();

    if (result = vkQueuePresentKHR(sCommandQueues[0], &presentI);
        result != VK_SUCCESS) {
      GetLogger()->error("Error presenting swapchains: {}",
                         iris::to_string(result));
    }
  }

  sFrameNum += 1;
  sFrameIndex = sFrameNum % sNumWindowFramesBuffered;
  sInFrame = false;
} // iris::Renderer::EndFrame

void iris::Renderer::AddRenderable(Component::Renderable renderable) noexcept {
  sRenderables.push_back(std::move(renderable));
} // AddRenderable

tl::expected<iris::Renderer::CommandQueue, std::system_error>
iris::Renderer::AcquireCommandQueue(
  std::chrono::milliseconds timeout) noexcept {
  IRIS_LOG_ENTER();
  Expects(sDevice != VK_NULL_HANDLE);
  std::unique_lock<std::timed_mutex> lock(sCommandQueueMutex, timeout);
  if (!lock) return tl::unexpected(std::system_error(Error::kTimeout));

  if (sCommandQueueHead == sCommandQueues.size() &&
      sCommandQueueFree > sCommandQueues.size()) {
    return tl::unexpected(std::system_error(Error::kNoCommandQueuesFree,
                                            "all command queues are in use"));
  }

  CommandQueue commandQueue;

  if (sCommandQueueHead < sCommandQueues.size()) {
    commandQueue.id = sCommandQueueHead++;
  } else {
    return tl::unexpected(std::system_error(Error::kNotImplemented));
  }

  commandQueue.queueFamilyIndex = sQueueFamilyIndex;
  commandQueue.queue = sCommandQueues[commandQueue.id];
  commandQueue.commandPool = sCommandPools[commandQueue.id];
  commandQueue.submitFence = sCommandFences[commandQueue.id];

  IRIS_LOG_LEAVE();
  return commandQueue;
} // iris::Renderer::AcquireCommandQueue

tl::expected<void, std::system_error> iris::Renderer::ReleaseCommandQueue(
  CommandQueue& queue, std::chrono::milliseconds timeout) noexcept {
  IRIS_LOG_ENTER();
  std::unique_lock<std::timed_mutex> lock(sCommandQueueMutex, timeout);
  if (!lock) return tl::unexpected(std::system_error(Error::kTimeout));

  if (queue.id == sCommandQueues.size() - 1) {
    queue.id = UINT32_MAX;
    queue.queueFamilyIndex = UINT32_MAX;
    queue.queue = VK_NULL_HANDLE;
    queue.commandPool = VK_NULL_HANDLE;
    queue.submitFence = VK_NULL_HANDLE;
    sCommandQueueHead--;
  } else {
    GetLogger()->critical("not implemented");
    std::abort();
  }

  IRIS_LOG_LEAVE();
  return {};
} // iris::Renderer::ReleaseCommandQueue

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

tl::expected<void, std::system_error>
iris::Renderer::LoadFile(filesystem::path const& path) noexcept {
  IRIS_LOG_ENTER();

  class IOTask : public tbb::task {
  public:
    IOTask(filesystem::path p) noexcept(noexcept(std::move(p)))
      : path_(std::move(p)) {}

    tbb::task* execute() override {
      IRIS_LOG_ENTER();

      GetLogger()->debug("Loading {}", path_.string());
      auto const& ext = path_.extension();

      if (ext.compare(".json") == 0) {
        sIOContinuations.push(io::LoadJSON(path_));
      } else if (ext.compare(".gltf") == 0) {
        sIOContinuations.push(io::LoadGLTF(path_));
      } else {
        GetLogger()->error("Unhandled file extension '{}' for {}", ext.string(),
                           path_.string());
      }

      IRIS_LOG_LEAVE();
      return nullptr;
    }

  private:
    filesystem::path path_;
  }; // struct IOTask

  try {
    IOTask* task = new (tbb::task::allocate_root()) IOTask(path);
    tbb::task::enqueue(*task);
  } catch (std::exception const& e) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(
      make_error_code(Error::kFileLoadFailed),
      fmt::format("Enqueing IO task for {}: {}", path.string(), e.what())));
  }

  IRIS_LOG_LEAVE();
  return {};
} // iris::Renderer::LoadFile

tl::expected<void, std::system_error>
iris::Renderer::Control(iris::Control::Control const& controlMessage) noexcept {
  IRIS_LOG_ENTER();

  switch (controlMessage.type_case()) {
  case iris::Control::Control::TypeCase::kDisplays:
    for (int i = 0; i < controlMessage.displays().windows_size(); ++i) {
      CreateEmplaceWindow(controlMessage.displays().windows(i));
    }
    break;
  case iris::Control::Control::TypeCase::kWindow:
    CreateEmplaceWindow(controlMessage.window());
    break;
  case iris::Control::Control::TypeCase::kShaderToy:
    sIOContinuations.push(io::LoadShaderToy(controlMessage.shadertoy()));
    break;
  default:
    GetLogger()->error("Unsupported controlMessage message type {}",
                       controlMessage.type_case());
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(Error::kControlMessageInvalid,
                        fmt::format("Unsupported controlMessage type {}",
                                    controlMessage.type_case())));
    break;
  }

  IRIS_LOG_LEAVE();
  return {};
} // iris::Renderer::Control

