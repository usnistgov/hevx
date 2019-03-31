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
#include "glm/common.hpp"
#include "glm/gtc/matrix_transform.hpp"
#if PLATFORM_COMPILER_GCC
#pragma GCC diagnostic ignored "-Wshadow"
#endif
#include "glslang/Public/ShaderLang.h"
#include "gsl/gsl"
#include "io/gltf.h"
#include "io/json.h"
#include "io/shadertoy.h"
#include "protos.h"
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
#include "wsi/input.h"
#if PLATFORM_LINUX
#include "wsi/platform_window_x11.h"
#elif PLATFORM_WINDOWS
#include "wsi/platform_window_win32.h"
#endif
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

absl::flat_hash_map<std::string, iris::Window>& Windows() {
  static absl::flat_hash_map<std::string, iris::Window> sWindows;
  return sWindows;
} // Windows

static std::uint32_t sQueueFamilyIndex{UINT32_MAX};
static absl::InlinedVector<VkQueue, 16> sCommandQueues;
static absl::InlinedVector<VkCommandPool, 16> sCommandPools;
static absl::InlinedVector<VkFence, 16> sCommandFences;

static std::uint32_t sCommandQueueHead{1};
static std::uint32_t sCommandQueueFree{UINT32_MAX};
static std::timed_mutex sCommandQueueMutex;

static std::uint32_t const sNumRenderPassAttachments{4};
static std::uint32_t const sColorTargetAttachmentIndex{0};
static std::uint32_t const sColorResolveAttachmentIndex{1};
static std::uint32_t const sDepthStencilTargetAttachmentIndex{2};
static std::uint32_t const sDepthStencilResolveAttachmentIndex{3};

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

static VkBuffer sMatricesBuffer{VK_NULL_HANDLE};
static VmaAllocation sMatricesBufferAllocation{VK_NULL_HANDLE};
static VkDeviceSize sMatricesBufferSize = 0;

static VkBuffer sLightsBuffer{VK_NULL_HANDLE};
static VmaAllocation sLightsBufferAllocation{VK_NULL_HANDLE};
static VkDeviceSize sLightsBufferSize = 0;

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

static VkPipelineLayout sUIPipelineLayout{VK_NULL_HANDLE};
static VkPipeline sUIPipeline{VK_NULL_HANDLE};
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
  if (auto result = vkAllocateCommandBuffers(sDevice, &commandBufferAI,
                                             &commandBuffer);
      result != VK_SUCCESS) {
    GetLogger()->error("Cannot allocate command buffer: {}", to_string(result));
    return VK_NULL_HANDLE;
  }

  VkCommandBufferInheritanceInfo commandBufferII = {};
  commandBufferII.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
  commandBufferII.framebuffer = VK_NULL_HANDLE;

  VkCommandBufferBeginInfo commandBufferBI = {};
  commandBufferBI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  commandBufferBI.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
  commandBufferBI.pInheritanceInfo = &commandBufferII;

  vkBeginCommandBuffer(commandBuffer, &commandBufferBI);

  VkImageMemoryBarrier barrier = {};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.srcAccessMask = 0;
  barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex =
    VK_QUEUE_FAMILY_IGNORED;
  barrier.image = dst;
  barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

  vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                       VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &barrier);

  VkImageCopy copy = {};
  copy.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  copy.srcOffset = {0, 0, 0};
  copy.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  copy.dstOffset = {0, 0, 0};
  copy.extent = extent;

  vkCmdCopyImage(commandBuffer, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst,
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

  barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  barrier.dstAccessMask = 0;
  barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

  vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                       VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &barrier);

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
  if (auto result = vkAllocateCommandBuffers(sDevice, &commandBufferAI,
                                             &commandBuffer);
      result != VK_SUCCESS) {
    GetLogger()->error("Cannot allocate command buffer: {}", to_string(result));
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
                    renderable.pipeline);

  vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          renderable.pipelineLayout, 0 /* firstSet */, 1,
                          &sGlobalDescriptorSet, 0, nullptr);

  vkCmdPushConstants(commandBuffer, renderable.pipelineLayout,
                     VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                     0, pushConstants.size(), pushConstants.data());

  vkCmdSetViewport(commandBuffer, 0, 1, pViewport);
  vkCmdSetScissor(commandBuffer, 0, 1, pScissor);

  if (renderable.descriptorSet != VK_NULL_HANDLE) {
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            renderable.pipelineLayout, 1 /* firstSet */, 1,
                            &renderable.descriptorSet, 0, nullptr);
  }

  if (renderable.vertexBuffer != VK_NULL_HANDLE) {
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &renderable.vertexBuffer,
                           &renderable.vertexBufferBindingOffset);
  }

  if (renderable.indexBuffer != VK_NULL_HANDLE) {
    vkCmdBindIndexBuffer(commandBuffer, renderable.indexBuffer,
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
        renderable.vertexBuffer, renderable.vertexBufferAllocation,
        renderable.vertexBufferSize,
        drawData->TotalVtxCount * sizeof(ImDrawVert),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU)) {
    std::tie(renderable.vertexBuffer, renderable.vertexBufferAllocation,
             renderable.vertexBufferSize) = *vb;
    NameObject(VK_OBJECT_TYPE_BUFFER, renderable.vertexBuffer,
               (title + "::uiRenderable.vertexBuffer").c_str());
  } else {
    GetLogger()->warn(
      "Unable to create/resize ui vertex buffer for window {}: {}", title,
      vb.error().what());
    return;
  }

  if (auto ib = ReallocateBuffer(
        renderable.indexBuffer, renderable.indexBufferAllocation,
        renderable.indexBufferSize, drawData->TotalIdxCount * sizeof(ImDrawIdx),
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU)) {
    std::tie(renderable.indexBuffer, renderable.indexBufferAllocation,
             renderable.indexBufferSize) = *ib;
    NameObject(VK_OBJECT_TYPE_BUFFER, renderable.indexBuffer,
               (title + "::uiRenderable.indexBuffer").c_str());
  } else {
    GetLogger()->warn(
      "Unable to create/resize ui index buffer for window {}: {}", title,
      ib.error().what());
    return;
  }

  ImDrawVert* pVertices;
  ImDrawIdx* pIndices;

  if (auto ptr = MapMemory<ImDrawVert*>(renderable.vertexBufferAllocation)) {
    pVertices = std::move(*ptr);
  } else {
    GetLogger()->warn("Unable to map ui vertex buffer for window {}: {}", title,
                      ptr.error().what());
    return;
  }

  if (auto ptr = MapMemory<ImDrawIdx*>(renderable.indexBufferAllocation)) {
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

  UnmapMemory(renderable.vertexBufferAllocation);
  UnmapMemory(renderable.indexBufferAllocation);
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
    VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME,
    VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME,
    VK_KHR_MAINTENANCE2_EXTENSION_NAME,
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
#if 0 // TODO: which GPUs support this?
    VK_KHR_MULTIVIEW_EXTENSION_NAME
#endif
  }};

  if ((options & Options::kEnableRayTracing) == Options::kEnableRayTracing) {
    physicalDeviceExtensionNames.push_back(VK_NV_RAY_TRACING_EXTENSION_NAME);
  }

#if PLATFORM_LINUX
  ::setenv(
    "VK_LAYER_PATH",
    (iris::kVulkanSDKDirectory + "/etc/vulkan/explicit_layer.d"s).c_str(), 0);
  GetLogger()->debug("VK_LAYER_PATH: {}", ::getenv("VK_LAYER_PATH"));
#endif

  flextVkInit();

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

  absl::FixedArray<VkSubpassDependency> dependencies{
    VkSubpassDependency{
      VK_SUBPASS_EXTERNAL,                           // srcSubpass
      0,                                             // dstSubpass
      VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,          // srcStageMask
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, // dstStageMask
      VK_ACCESS_MEMORY_READ_BIT,                     // srcAccessMask
      VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, // dstAccessMask
      VK_DEPENDENCY_BY_REGION_BIT             // dependencyFlags
    },
    VkSubpassDependency{
      0,                                             // srcSubpass
      VK_SUBPASS_EXTERNAL,                           // dstSubpass
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, // srcStageMask
      VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,          // dstStageMask
      VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, // srcAccessMask
      VK_ACCESS_MEMORY_READ_BIT,              // dstAccessMask
      VK_DEPENDENCY_BY_REGION_BIT             // dependencyFlags
    }};

  VkRenderPassCreateInfo renderPassCI = {};
  renderPassCI.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  renderPassCI.attachmentCount = attachments.size();
  renderPassCI.pAttachments = attachments.data();
  renderPassCI.subpassCount = 1;
  renderPassCI.pSubpasses = &subpass;
  renderPassCI.dependencyCount = dependencies.size();
  renderPassCI.pDependencies = dependencies.data();

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
      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, // stageFlags
      nullptr // pImmutableSamplers
    },
    // This is the LightsBuffer
    {
      1,                                 // binding
      VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, // descriptorType
      1,                                 // descriptorCount
      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, // stageFlags
      nullptr // pImmutableSamplers
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

  if (auto bas = AllocateBuffer(sizeof(MatricesBuffer),
                                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                VMA_MEMORY_USAGE_CPU_TO_GPU)) {
    std::tie(sMatricesBuffer, sMatricesBufferAllocation, sMatricesBufferSize) =
      *bas;
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(bas.error());
  }

  NameObject(VK_OBJECT_TYPE_BUFFER, sMatricesBuffer, "sMatricesBuffer");

  if (auto bas = AllocateBuffer(sizeof(LightsBuffer),
                                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                VMA_MEMORY_USAGE_CPU_TO_GPU)) {
    std::tie(sLightsBuffer, sLightsBufferAllocation, sLightsBufferSize) =
      *bas;
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(bas.error());
  }

  NameObject(VK_OBJECT_TYPE_BUFFER, sLightsBuffer, "sLightsBuffer");

  VkDescriptorBufferInfo matricesBufferInfo = {};
  matricesBufferInfo.buffer = sMatricesBuffer;
  matricesBufferInfo.offset = 0;
  matricesBufferInfo.range = VK_WHOLE_SIZE;

  VkDescriptorBufferInfo lightsBufferInfo = {};
  lightsBufferInfo.buffer = sLightsBuffer;
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

  if (auto s = CompileShaderFromSource(sUIVertexShaderSource,
                                       VK_SHADER_STAGE_VERTEX_BIT)) {
    shaders[0] = {*s, VK_SHADER_STAGE_VERTEX_BIT};
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(s.error());
  }

  if (auto s = CompileShaderFromSource(sUIFragmentShaderSource,
                                       VK_SHADER_STAGE_FRAGMENT_BIT)) {
    shaders[1] = {*s, VK_SHADER_STAGE_FRAGMENT_BIT};
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(s.error());
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

  if (auto lp = CreateGraphicsPipeline(
        shaders, vertexInputBindingDescriptions,
        vertexInputAttributeDescriptions, inputAssemblyStateCI, viewportStateCI,
        rasterizationStateCI, multisampleStateCI, depthStencilStateCI,
        colorBlendAttachmentStates, dynamicStates, 0,
        gsl::make_span(&sUIDescriptorSetLayout, 1))) {
    std::tie(sUIPipelineLayout, sUIPipeline) = *lp;
  } else {
    using namespace std::string_literals;
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(
      lp.error().code(), "Cannot create UI pipeline: "s + lp.error().what()));
  }

  NameObject(VK_OBJECT_TYPE_PIPELINE_LAYOUT, sUIPipelineLayout,
             "sUIPipelineLayout");
  NameObject(VK_OBJECT_TYPE_PIPELINE, sUIPipeline, "sUIPipeline");

  sRunning = true;
  IRIS_LOG_LEAVE();
  return {};
} // iris::Renderer::Create

bool iris::Renderer::IsRunning() noexcept {
  return sRunning;
} // iris::Renderer::IsRunning

void iris::Renderer::Terminate() noexcept {
  IRIS_LOG_ENTER();
  sRunning = false;
  IRIS_LOG_LEAVE();
} // iris::Renderer::Terminate

tl::expected<iris::Window, std::exception>
iris::Renderer::CreateWindow(gsl::czstring<> title, wsi::Offset2D offset,
                             wsi::Extent2D extent, glm::vec4 const& clearColor,
                             Window::Options const& options, int display,
                             std::uint32_t numFrames) noexcept {
  IRIS_LOG_ENTER();
  Expects(sInstance != VK_NULL_HANDLE);
  Expects(sPhysicalDevice != VK_NULL_HANDLE);
  Expects(sDevice != VK_NULL_HANDLE);

  Window window(title, {clearColor.r, clearColor.g, clearColor.b, clearColor.a},
                numFrames);
  window.showUI =
    (options & Window::Options::kShowUI) == Window::Options::kShowUI;

  wsi::PlatformWindow::Options platformOptions =
    wsi::PlatformWindow::Options::kSizeable;
  if ((options & Window::Options::kDecorated) == Window::Options::kDecorated) {
    platformOptions |= wsi::PlatformWindow::Options::kDecorated;
  }

  if (auto win =
        wsi::PlatformWindow::Create(title, std::move(offset), std::move(extent),
                                    platformOptions, display)) {
    window.platformWindow = std::move(*win);
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(win.error());
  }

#if defined(VK_USE_PLATFORM_XCB_KHR)

  VkXcbSurfaceCreateInfoKHR sci = {};
  sci.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
  std::tie(sci.connection, sci.window) = window.platformWindow.NativeHandle();

  if (auto result =
        vkCreateXcbSurfaceKHR(sInstance, &sci, nullptr, &window.surface);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(make_error_code(result), "Cannot create surface"));
  }

#elif defined(VK_USE_PLATFORM_WIN32_KHR)

  VkWin32SurfaceCreateInfoKHR sci = {};
  sci.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
  std::tie(sci.hinstance, sci.hwnd) = window.platformWindow.NativeHandle();

  if (auto result =
        vkCreateWin32SurfaceKHR(sInstance, &sci, nullptr, &window.surface);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(make_error_code(result), "Cannot create surface"));
  }

#endif

  NameObject(VK_OBJECT_TYPE_SURFACE_KHR, window.surface,
             fmt::format("{}.surface", title).c_str());

  VkBool32 surfaceSupported;
  if (auto result = vkGetPhysicalDeviceSurfaceSupportKHR(
        sPhysicalDevice, sQueueFamilyIndex, window.surface, &surfaceSupported);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(make_error_code(result),
                        "Cannot check for physical device surface support"));
  }

  if (surfaceSupported == VK_FALSE) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(Error::kSurfaceNotSupported,
                        "Surface is not supported by physical device."));
  }

  bool formatSupported = false;
  if (auto surfaceFormats =
        GetPhysicalDeviceSurfaceFormats(sPhysicalDevice, window.surface)) {
    if (surfaceFormats->size() == 1 &&
        (*surfaceFormats)[0].format == VK_FORMAT_UNDEFINED) {
      formatSupported = true;
    } else {
      for (auto&& supported : *surfaceFormats) {
        if (supported.format == sSurfaceColorFormat.format &&
            supported.colorSpace == sSurfaceColorFormat.colorSpace) {
          formatSupported = true;
          break;
        }
      }
    }
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(surfaceFormats.error());
  }

  if (!formatSupported) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(Error::kSurfaceNotSupported,
                        "Surface format is not supported by physical device"));
  }

  VkSemaphoreCreateInfo semaphoreCI = {};
  semaphoreCI.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

  VkCommandPoolCreateInfo commandPoolCI = {};
  commandPoolCI.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  commandPoolCI.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  commandPoolCI.queueFamilyIndex = sQueueFamilyIndex;

  VkCommandBufferAllocateInfo commandBufferAI = {};
  commandBufferAI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  commandBufferAI.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  commandBufferAI.commandBufferCount = 1;

  for (auto&& [i, frame] : enumerate(window.frames)) {
    if (auto result = vkCreateSemaphore(sDevice, &semaphoreCI, nullptr,
                                        &frame.imageAvailable);
        result != VK_SUCCESS) {
      IRIS_LOG_LEAVE();
      return tl::unexpected(std::system_error(
        make_error_code(result), "Cannot create image available semaphore"));
    }

    NameObject(VK_OBJECT_TYPE_SEMAPHORE, frame.imageAvailable,
               fmt::format("{}.frames[{}].imageAvailable", title, i).c_str());

    if (auto result = vkCreateCommandPool(sDevice, &commandPoolCI, nullptr,
                                          &frame.commandPool);
        result != VK_SUCCESS) {
      IRIS_LOG_LEAVE();
      return tl::unexpected(std::system_error(make_error_code(result),
                                              "Cannot create command pool"));
    }

    NameObject(VK_OBJECT_TYPE_COMMAND_POOL, frame.commandPool,
               fmt::format("{}.frames[{}].commandPool", title, i).c_str());

    commandBufferAI.commandPool = frame.commandPool;

    if (auto result = vkAllocateCommandBuffers(sDevice, &commandBufferAI,
                                               &frame.commandBuffer);
        result != VK_SUCCESS) {
      IRIS_LOG_LEAVE();
      return tl::unexpected(std::system_error(
        make_error_code(result), "Cannot allocate command buffer"));
    }

    NameObject(VK_OBJECT_TYPE_COMMAND_BUFFER, frame.commandBuffer,
               fmt::format("{}.frames[{}].commandBuffer", title, i).c_str());
  }

  if (auto result = ResizeWindow(window, {extent.width, extent.height});
      !result) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(result.error());
  }

  window.uiContext.reset(ImGui::CreateContext());
  ImGui::SetCurrentContext(window.uiContext.get());
  ImGui::StyleColorsDark();

  ImGuiIO& io = ImGui::GetIO();

  io.BackendRendererName = "hevx::iris";
  io.BackendRendererName = "";

  io.Fonts->AddFontFromFileTTF(
    (kIRISContentDirectory + "/assets/fonts/SourceSansPro-Regular.ttf"s)
      .c_str(),
    16.f);

  unsigned char* pixels;
  int width, height, bytesPerPixel;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height, &bytesPerPixel);

  VkImage fontTexture;
  VmaAllocation fontTextureAllocation;

  if (auto ia =
        CreateImage(sCommandPools[0], sCommandQueues[0], sCommandFences[0],
                    VK_FORMAT_R8G8B8A8_UNORM,
                    VkExtent2D{gsl::narrow_cast<std::uint32_t>(width),
                               gsl::narrow_cast<std::uint32_t>(height)},
                    VK_IMAGE_USAGE_SAMPLED_BIT, VMA_MEMORY_USAGE_GPU_ONLY,
                    gsl::not_null(reinterpret_cast<std::byte*>(pixels)),
                    gsl::narrow_cast<std::uint32_t>(bytesPerPixel))) {
    std::tie(fontTexture, fontTextureAllocation) = *ia;
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(ia.error());
  }

  window.uiRenderable.images.push_back(fontTexture);
  window.uiRenderable.imageAllocations.push_back(fontTextureAllocation);

  NameObject(
    VK_OBJECT_TYPE_IMAGE, window.uiRenderable.images[0],
    fmt::format("{}.uiRenderable.images[0] (fontTexture)", title).c_str());

  VkImageViewCreateInfo imageViewCI = {};
  imageViewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  imageViewCI.image = window.uiRenderable.images[0];
  imageViewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
  imageViewCI.format = VK_FORMAT_R8G8B8A8_UNORM;
  imageViewCI.components = {
    VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
    VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
  imageViewCI.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}; 

  VkImageView fontTextureView;
  if (auto result =
        vkCreateImageView(sDevice, &imageViewCI, nullptr, &fontTextureView);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(make_error_code(result), "Cannot create image view"));
  }

  window.uiRenderable.imageViews.push_back(fontTextureView);

  NameObject(
    VK_OBJECT_TYPE_IMAGE_VIEW, window.uiRenderable.imageViews[0],
    fmt::format("{}.uiRenderable.views[0] (fontTextureView)", title).c_str());

  VkSamplerCreateInfo samplerCI = {};
  samplerCI.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerCI.magFilter = VK_FILTER_LINEAR;
  samplerCI.minFilter = VK_FILTER_LINEAR;
  samplerCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  samplerCI.mipLodBias = 0.f;
  samplerCI.anisotropyEnable = VK_FALSE;
  samplerCI.maxAnisotropy = 1;
  samplerCI.compareEnable = VK_FALSE;
  samplerCI.compareOp = VK_COMPARE_OP_ALWAYS;
  samplerCI.minLod = -1000.f;
  samplerCI.maxLod = 1000.f;
  samplerCI.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
  samplerCI.unnormalizedCoordinates = VK_FALSE;

  VkSampler fontTextureSampler;
  if (auto result =
        vkCreateSampler(sDevice, &samplerCI, nullptr, &fontTextureSampler);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(make_error_code(result), "Cannot create sampler"));
  }

  window.uiRenderable.imageSamplers.push_back(fontTextureSampler);

  NameObject(
    VK_OBJECT_TYPE_SAMPLER, window.uiRenderable.imageSamplers[0],
    fmt::format("{}.uiRenderable.samplers[0] (fontTextureSampler)", title)
      .c_str());

  io.KeyMap[ImGuiKey_Tab] = static_cast<int>(wsi::Keys::kTab);
  io.KeyMap[ImGuiKey_LeftArrow] = static_cast<int>(wsi::Keys::kLeft);
  io.KeyMap[ImGuiKey_RightArrow] = static_cast<int>(wsi::Keys::kRight);
  io.KeyMap[ImGuiKey_UpArrow] = static_cast<int>(wsi::Keys::kUp);
  io.KeyMap[ImGuiKey_DownArrow] = static_cast<int>(wsi::Keys::kDown);
  io.KeyMap[ImGuiKey_PageUp] = static_cast<int>(wsi::Keys::kPageUp);
  io.KeyMap[ImGuiKey_PageDown] = static_cast<int>(wsi::Keys::kPageDown);
  io.KeyMap[ImGuiKey_Home] = static_cast<int>(wsi::Keys::kHome);
  io.KeyMap[ImGuiKey_End] = static_cast<int>(wsi::Keys::kEnd);
  io.KeyMap[ImGuiKey_Insert] = static_cast<int>(wsi::Keys::kInsert);
  io.KeyMap[ImGuiKey_Delete] = static_cast<int>(wsi::Keys::kDelete);
  io.KeyMap[ImGuiKey_Backspace] = static_cast<int>(wsi::Keys::kBackspace);
  io.KeyMap[ImGuiKey_Space] = static_cast<int>(wsi::Keys::kSpace);
  io.KeyMap[ImGuiKey_Enter] = static_cast<int>(wsi::Keys::kEnter);
  io.KeyMap[ImGuiKey_Escape] = static_cast<int>(wsi::Keys::kEscape);
  io.KeyMap[ImGuiKey_A] = static_cast<int>(wsi::Keys::kA);
  io.KeyMap[ImGuiKey_C] = static_cast<int>(wsi::Keys::kC);
  io.KeyMap[ImGuiKey_V] = static_cast<int>(wsi::Keys::kV);
  io.KeyMap[ImGuiKey_X] = static_cast<int>(wsi::Keys::kX);
  io.KeyMap[ImGuiKey_Y] = static_cast<int>(wsi::Keys::kY);
  io.KeyMap[ImGuiKey_Z] = static_cast<int>(wsi::Keys::kZ);

  window.platformWindow.OnResize(
    [&window](wsi::Extent2D const&) { window.resized = true; });
  window.platformWindow.OnClose([]() { Terminate(); });
  window.platformWindow.Show();

  Ensures(window.surface != VK_NULL_HANDLE);
  Ensures(window.swapchain != VK_NULL_HANDLE);
  Ensures(!window.colorImages.empty());
  Ensures(!window.colorImageViews.empty());
  Ensures(window.depthStencilImage != VK_NULL_HANDLE);
  Ensures(window.depthStencilImageAllocation != VK_NULL_HANDLE);
  Ensures(window.depthStencilImageView != VK_NULL_HANDLE);
  Ensures(window.colorTarget != VK_NULL_HANDLE);
  Ensures(window.colorTargetAllocation != VK_NULL_HANDLE);
  Ensures(window.colorTargetView != VK_NULL_HANDLE);
  Ensures(window.depthStencilTarget != VK_NULL_HANDLE);
  Ensures(window.depthStencilTargetAllocation != VK_NULL_HANDLE);
  Ensures(window.depthStencilTargetView != VK_NULL_HANDLE);
  Ensures(!window.frames.empty());

  IRIS_LOG_LEAVE();
  return std::move(window);
} // iris::Renderer::CreateWindow

tl::expected<void, std::system_error>
iris::Renderer::ResizeWindow(Window& window, VkExtent2D newExtent) noexcept {
  IRIS_LOG_ENTER();
  Expects(sPhysicalDevice != VK_NULL_HANDLE);
  Expects(sDevice != VK_NULL_HANDLE);

  GetLogger()->debug("Resizing window to ({}x{})", newExtent.width,
                     newExtent.height);

  VkSurfaceCapabilities2KHR surfaceCapabilities = {};
  surfaceCapabilities.sType = VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR;

  VkPhysicalDeviceSurfaceInfo2KHR surfaceInfo = {};
  surfaceInfo.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SURFACE_INFO_2_KHR;
  surfaceInfo.surface = window.surface;

  if (auto result = vkGetPhysicalDeviceSurfaceCapabilities2KHR(
        sPhysicalDevice, &surfaceInfo, &surfaceCapabilities);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(make_error_code(result),
                        "Cannot get physical device surface capabilities"));
  }

  VkSurfaceCapabilitiesKHR caps = surfaceCapabilities.surfaceCapabilities;

  newExtent.width = caps.currentExtent.width == UINT32_MAX
                      ? glm::clamp(newExtent.width, caps.minImageExtent.width,
                                   caps.maxImageExtent.width)
                      : caps.currentExtent.width;

  newExtent.height =
    caps.currentExtent.height == UINT32_MAX
      ? glm::clamp(newExtent.height, caps.minImageExtent.height,
                   caps.maxImageExtent.height)
      : caps.currentExtent.height;

  VkViewport newViewport{
    0.f,                                  // x
    0.f,                                  // y
    static_cast<float>(newExtent.width),  // width
    static_cast<float>(newExtent.height), // height
    0.f,                                  // minDepth
    1.f,                                  // maxDepth
  };

  VkRect2D newScissor{{0, 0}, newExtent};

  VkSwapchainCreateInfoKHR swapchainCI = {};
  swapchainCI.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  swapchainCI.surface = window.surface;
  swapchainCI.minImageCount = caps.minImageCount;
  swapchainCI.imageFormat = sSurfaceColorFormat.format;
  swapchainCI.imageColorSpace = sSurfaceColorFormat.colorSpace;
  swapchainCI.imageExtent = newExtent;
  swapchainCI.imageArrayLayers = 1;
  swapchainCI.imageUsage =
    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  swapchainCI.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  swapchainCI.queueFamilyIndexCount = 0;
  swapchainCI.pQueueFamilyIndices = nullptr;
  swapchainCI.preTransform = caps.currentTransform;
  swapchainCI.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  swapchainCI.presentMode = sSurfacePresentMode;
  swapchainCI.clipped = VK_TRUE;
  swapchainCI.oldSwapchain = window.swapchain;

  VkSwapchainKHR newSwapchain;
  if (auto result =
        vkCreateSwapchainKHR(sDevice, &swapchainCI, nullptr, &newSwapchain);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(make_error_code(result), "Cannot create swapchain"));
  }

  std::uint32_t numSwapchainImages;
  if (auto result = vkGetSwapchainImagesKHR(sDevice, newSwapchain,
                                            &numSwapchainImages, nullptr);
      result != VK_SUCCESS) {
    vkDestroySwapchainKHR(sDevice, newSwapchain, nullptr);
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(make_error_code(result),
                                            "Cannot get swapchain images"));
  }

  if (numSwapchainImages != window.colorImages.size()) {
    vkDestroySwapchainKHR(sDevice, newSwapchain, nullptr);
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(
      Error::kWindowResizeFailed,
      "New number of swapchain images not equal to old number"));
  }

  if (numSwapchainImages != window.frames.size()) {
    vkDestroySwapchainKHR(sDevice, newSwapchain, nullptr);
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(
      Error::kWindowResizeFailed,
      "New number of swapchain images not equal to number of frames"));
  }

  absl::FixedArray<VkImage> newColorImages(numSwapchainImages);
  if (auto result = vkGetSwapchainImagesKHR(
        sDevice, newSwapchain, &numSwapchainImages, newColorImages.data());
      result != VK_SUCCESS) {
    vkDestroySwapchainKHR(sDevice, newSwapchain, nullptr);
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(make_error_code(result),
                                            "Cannot get swapchain images"));
  }

  VkImageViewCreateInfo imageViewCI = {};
  imageViewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  imageViewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
  imageViewCI.format = sSurfaceColorFormat.format;
  imageViewCI.components = {
    VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
    VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
  imageViewCI.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

  absl::FixedArray<VkImageView> newColorImageViews(numSwapchainImages);
  for (auto&& [i, view] : enumerate(newColorImageViews)) {
    imageViewCI.image = newColorImages[i];
    if (auto result = vkCreateImageView(sDevice, &imageViewCI, nullptr, &view);
        result != VK_SUCCESS) {
      vkDestroySwapchainKHR(sDevice, newSwapchain, nullptr);
      IRIS_LOG_LEAVE();
      return tl::unexpected(std::system_error(
        make_error_code(result), "Cannot get swapchain image view"));
    }
  }

  VkImage newDepthStencilImage;
  VmaAllocation newDepthStencilImageAllocation;
  VkImageView newDepthStencilImageView;

  if (auto iav = AllocateImageAndView(
        sSurfaceDepthStencilFormat, newExtent, 1, 1, VK_SAMPLE_COUNT_1_BIT,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_TILING_OPTIMAL,
        VMA_MEMORY_USAGE_GPU_ONLY, {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1})) {
    std::tie(newDepthStencilImage, newDepthStencilImageAllocation,
             newDepthStencilImageView) = *iav;
  } else {
    for (auto&& v : newColorImageViews) vkDestroyImageView(sDevice, v, nullptr);
    vkDestroySwapchainKHR(sDevice, newSwapchain, nullptr);
    IRIS_LOG_LEAVE();
    return tl::unexpected(iav.error());
  }

  VkImage newColorTarget;
  VmaAllocation newColorTargetAllocation;
  VkImageView newColorTargetView;

  if (auto iav = AllocateImageAndView(
        sSurfaceColorFormat.format, newExtent, 1, 1, sSurfaceSampleCount,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
          VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
        VK_IMAGE_TILING_OPTIMAL, VMA_MEMORY_USAGE_GPU_ONLY,
        {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1})) {
    std::tie(newColorTarget, newColorTargetAllocation, newColorTargetView) =
      *iav;
  } else {
    vkDestroyImageView(sDevice, newDepthStencilImageView, nullptr);
    vmaDestroyImage(sAllocator, newDepthStencilImage,
                    newDepthStencilImageAllocation);
    for (auto&& v : newColorImageViews) vkDestroyImageView(sDevice, v, nullptr);
    vkDestroySwapchainKHR(sDevice, newSwapchain, nullptr);
    IRIS_LOG_LEAVE();
    return tl::unexpected(iav.error());
  }

  VkImage newDepthStencilTarget;
  VmaAllocation newDepthStencilTargetAllocation;
  VkImageView newDepthStencilTargetView;

  if (auto iav = AllocateImageAndView(
        sSurfaceDepthStencilFormat, newExtent, 1, 1, sSurfaceSampleCount,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_TILING_OPTIMAL,
        VMA_MEMORY_USAGE_GPU_ONLY, {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1})) {
    std::tie(newDepthStencilTarget, newDepthStencilTargetAllocation,
             newDepthStencilTargetView) = *iav;
  } else {
    vkDestroyImageView(sDevice, newColorTargetView, nullptr);
    vmaDestroyImage(sAllocator, newColorTarget, newColorTargetAllocation);
    vkDestroyImageView(sDevice, newDepthStencilImageView, nullptr);
    vmaDestroyImage(sAllocator, newDepthStencilImage,
                    newDepthStencilImageAllocation);
    for (auto&& v : newColorImageViews) vkDestroyImageView(sDevice, v, nullptr);
    vkDestroySwapchainKHR(sDevice, newSwapchain, nullptr);
    IRIS_LOG_LEAVE();
    return tl::unexpected(iav.error());
  }

  if (auto result =
        TransitionImage(sCommandPools[0], sCommandQueues[0], sCommandFences[0],
                        newColorTarget, VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 1, 1);
      !result) {
    vkDestroyImageView(sDevice, newColorTargetView, nullptr);
    vmaDestroyImage(sAllocator, newColorTarget, newColorTargetAllocation);
    vkDestroyImageView(sDevice, newDepthStencilImageView, nullptr);
    vmaDestroyImage(sAllocator, newDepthStencilImage,
                    newDepthStencilImageAllocation);
    for (auto&& v : newColorImageViews) vkDestroyImageView(sDevice, v, nullptr);
    vkDestroySwapchainKHR(sDevice, newSwapchain, nullptr);
    IRIS_LOG_LEAVE();
    return tl::unexpected(result.error());
  }

  if (auto result =
        TransitionImage(sCommandPools[0], sCommandQueues[0], sCommandFences[0],
                        newDepthStencilTarget, VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, 1, 1);
      !result) {
    vkDestroyImageView(sDevice, newColorTargetView, nullptr);
    vmaDestroyImage(sAllocator, newColorTarget, newColorTargetAllocation);
    vkDestroyImageView(sDevice, newDepthStencilImageView, nullptr);
    vmaDestroyImage(sAllocator, newDepthStencilImage,
                    newDepthStencilImageAllocation);
    for (auto&& v : newColorImageViews) vkDestroyImageView(sDevice, v, nullptr);
    vkDestroySwapchainKHR(sDevice, newSwapchain, nullptr);
    IRIS_LOG_LEAVE();
    return tl::unexpected(result.error());
  }

  absl::FixedArray<VkImageView> attachments(sNumRenderPassAttachments);
  attachments[sColorTargetAttachmentIndex] = newColorTargetView;
  attachments[sDepthStencilTargetAttachmentIndex] = newDepthStencilTargetView;
  attachments[sDepthStencilResolveAttachmentIndex] = newDepthStencilImageView;

  VkFramebufferCreateInfo framebufferCI = {};
  framebufferCI.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
  framebufferCI.renderPass = sRenderPass;
  framebufferCI.attachmentCount = attachments.size();
  framebufferCI.width = newExtent.width;
  framebufferCI.height = newExtent.height;
  framebufferCI.layers = 1;

  absl::FixedArray<VkFramebuffer> newFramebuffers(numSwapchainImages);

  for (auto&& [i, framebuffer] : enumerate(newFramebuffers)) {
    attachments[sColorResolveAttachmentIndex] = newColorImageViews[i];
    framebufferCI.pAttachments = attachments.data();

    if (auto result =
          vkCreateFramebuffer(sDevice, &framebufferCI, nullptr, &framebuffer);
        result != VK_SUCCESS) {
      vkDestroyImageView(sDevice, newColorTargetView, nullptr);
      vmaDestroyImage(sAllocator, newColorTarget, newColorTargetAllocation);
      vkDestroyImageView(sDevice, newDepthStencilImageView, nullptr);
      vmaDestroyImage(sAllocator, newDepthStencilImage,
                      newDepthStencilImageAllocation);
      for (auto&& v : newColorImageViews)
        vkDestroyImageView(sDevice, v, nullptr);
      vkDestroySwapchainKHR(sDevice, newSwapchain, nullptr);
      IRIS_LOG_LEAVE();
      return tl::unexpected(std::system_error(make_error_code(result),
                                              "Cannot create framebuffer"));
    }
  }

  glm::mat4 const newProjectionMatrix =
    glm::perspectiveFov(60.f, static_cast<float>(newExtent.width),
                        static_cast<float>(newExtent.height), .001f, 1000.f);

  if (window.swapchain != VK_NULL_HANDLE) {
    GetLogger()->trace("ResizeWindow: releasing old resources");
    for (auto&& frame : window.frames) {
      vkDestroyFramebuffer(sDevice, frame.framebuffer, nullptr);
    }
    vkDestroyImageView(sDevice, window.colorTargetView, nullptr);
    vmaDestroyImage(sAllocator, window.colorTarget,
                    window.colorTargetAllocation);
    vkDestroyImageView(sDevice, window.depthStencilImageView, nullptr);
    vmaDestroyImage(sAllocator, window.depthStencilImage,
                    window.depthStencilImageAllocation);
    for (auto&& view : window.colorImageViews) {
      vkDestroyImageView(sDevice, view, nullptr);
    }
    vkDestroySwapchainKHR(sDevice, window.swapchain, nullptr);
  }

  window.extent = newExtent;
  window.viewport = newViewport;
  window.scissor = newScissor;

  window.swapchain = newSwapchain;
  NameObject(VK_OBJECT_TYPE_SWAPCHAIN_KHR, window.swapchain,
             fmt::format("{}.swapchain", window.title).c_str());

  std::copy_n(newColorImages.begin(), numSwapchainImages,
              window.colorImages.begin());
  for (auto&& [i, image] : enumerate(window.colorImages)) {
    NameObject(VK_OBJECT_TYPE_IMAGE, image,
               fmt::format("{}.colorImages[{}]", window.title, i).c_str());
  }

  std::copy_n(newColorImageViews.begin(), numSwapchainImages,
              window.colorImageViews.begin());
  for (auto&& [i, view] : enumerate(window.colorImageViews)) {
    NameObject(VK_OBJECT_TYPE_IMAGE_VIEW, view,
               fmt::format("{}.colorImageViews[{}]", window.title, i).c_str());
  }

  window.depthStencilImage = newDepthStencilImage;
  window.depthStencilImageAllocation = newDepthStencilImageAllocation;
  window.depthStencilImageView = newDepthStencilImageView;
  NameObject(VK_OBJECT_TYPE_IMAGE, window.depthStencilImage,
             fmt::format("{}.depthStencilImage", window.title).c_str());
  NameObject(VK_OBJECT_TYPE_IMAGE_VIEW, window.depthStencilImageView,
             fmt::format("{}.depthStencilImageView", window.title).c_str());

  window.colorTarget = newColorTarget;
  window.colorTargetAllocation = newColorTargetAllocation;
  window.colorTargetView = newColorTargetView;
  NameObject(VK_OBJECT_TYPE_IMAGE, window.colorTarget,
             fmt::format("{}.colorTarget", window.title).c_str());
  NameObject(VK_OBJECT_TYPE_IMAGE_VIEW, window.colorTargetView,
             fmt::format("{}.colorTargetView", window.title).c_str());

  window.depthStencilTarget = newDepthStencilTarget;
  window.depthStencilTargetAllocation = newDepthStencilTargetAllocation;
  window.depthStencilTargetView = newDepthStencilTargetView;
  NameObject(VK_OBJECT_TYPE_IMAGE, window.depthStencilTarget,
             fmt::format("{}.depthStencilTarget", window.title).c_str());
  NameObject(VK_OBJECT_TYPE_IMAGE_VIEW, window.depthStencilTargetView,
             fmt::format("{}.depthStencilTargetView", window.title).c_str());

  for (auto&& [i, frame] : enumerate(window.frames)) {
    frame.framebuffer = newFramebuffers[i];
    NameObject(
      VK_OBJECT_TYPE_FRAMEBUFFER, frame.framebuffer,
      fmt::format("{}.frames[{}].framebuffer", window.title, i).c_str());
  }

  window.projectionMatrix = newProjectionMatrix;
  window.projectionMatrixInverse = glm::inverse(window.projectionMatrix);

  IRIS_LOG_LEAVE();
  return {};
} // iris::Renderer::ResizeWindow

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

    if (auto ptr = MapMemory<MatricesBuffer*>(sMatricesBufferAllocation)) {
      (*ptr)->ProjectionMatrix = window.projectionMatrix;
      (*ptr)->ProjectionMatrixInverse = window.projectionMatrixInverse;
      UnmapMemory(sMatricesBufferAllocation);
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

    glm::mat4 const viewMatrix = glm::lookAt(
      glm::vec3(1.f, 1.f, -1.f), glm::vec3(0.f), glm::vec3(0.f, 1.f, 0.f));
    std::vector<Component::Renderable> renderables = sRenderables();

    for (auto&& renderable : renderables) {
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
        uiSamplerInfo.sampler = window.uiRenderable.imageSamplers[0];

        VkDescriptorImageInfo uiTextureInfo = {};
        uiTextureInfo.imageView = window.uiRenderable.imageViews[0];
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
            renderable.pipelineLayout = sUIPipelineLayout;
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
                       to_string(result));
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
      GetLogger()->error("Error presenting swapchains: {}", to_string(result));
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
