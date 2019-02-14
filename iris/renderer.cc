/*! \file
 * \brief \ref iris::Renderer definition.
 */
#include "config.h"

#include "absl/container/flat_hash_map.h"
#if PLATFORM_COMPILER_GCC
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#include "absl/container/inlined_vector.h"
#if PLATFORM_COMPILER_GCC
#pragma GCC diagnostic pop
#endif
#include "enumerate.h"
#include "error.h"
#include "glm/common.hpp"
#if PLATFORM_COMPILER_GCC
#pragma GCC diagnostic ignored "-Wshadow"
#endif
#include "glslang/Public/ShaderLang.h"
#include "gsl/gsl"
#include "io/json.h"
#include "protos.h"
#include "renderer.h"
#if PLATFORM_COMPILER_MSVC
#pragma warning(push)
#pragma warning(disable : 4127)
#endif
#include "spdlog/spdlog.h"
#if PLATFORM_COMPILER_MSVC
#pragma warning(pop)
#endif
#include "tbb/concurrent_queue.h"
#include "tbb/task_scheduler_init.h"
#include "tbb/task.h"
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
#include <fstream>
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

static bool sRunning{false};

static VkInstance sInstance{VK_NULL_HANDLE};
static VkDebugUtilsMessengerEXT sDebugUtilsMessenger{VK_NULL_HANDLE};
static VkPhysicalDevice sPhysicalDevice{VK_NULL_HANDLE};
static VkDevice sDevice{VK_NULL_HANDLE};
static VmaAllocator sAllocator{VK_NULL_HANDLE};

static std::uint32_t sGraphicsQueueFamilyIndex{UINT32_MAX};
static absl::InlinedVector<VkQueue, 16> sGraphicsCommandQueues;
static absl::InlinedVector<VkCommandPool, 16> sGraphicsCommandPools;
static absl::InlinedVector<VkFence, 16> sGraphicsCommandFences;

static VkRenderPass sRenderPass{VK_NULL_HANDLE};

static constexpr std::uint32_t const sNumRenderPassAttachments{4};
static constexpr std::uint32_t const sColorTargetAttachmentIndex{0};
static constexpr std::uint32_t const sColorResolveAttachmentIndex{1};
static constexpr std::uint32_t const sDepthStencilTargetAttachmentIndex{2};
static constexpr std::uint32_t const sDepthStencilResolveAttachmentIndex{3};

static constexpr VkSurfaceFormatKHR const sSurfaceColorFormat{
  VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
static constexpr VkFormat const sSurfaceDepthStencilFormat{
  VK_FORMAT_D32_SFLOAT};
static constexpr VkSampleCountFlagBits const sSurfaceSampleCount{
  VK_SAMPLE_COUNT_4_BIT};
static constexpr VkPresentModeKHR const sSurfacePresentMode{
  VK_PRESENT_MODE_FIFO_KHR};

static absl::flat_hash_map<std::string, iris::Window>& Windows() {
  static absl::flat_hash_map<std::string, iris::Window> sWindows;
  return sWindows;
} // Windows

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

static Renderables sRenderables;

static VkCommandBuffer
Render(iris::Renderer::Component::Renderable const& renderable,
       VkViewport* pViewport, VkRect2D* pScissor) noexcept {
  VkCommandBufferAllocateInfo commandBufferAI = {};
  commandBufferAI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  commandBufferAI.commandPool = sGraphicsCommandPools[0];
  commandBufferAI.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
  commandBufferAI.commandBufferCount = 1;

  VkCommandBuffer commandBuffer;
  if (auto result =
        vkAllocateCommandBuffers(sDevice, &commandBufferAI, &commandBuffer);
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

  vkCmdSetViewport(commandBuffer, 0, 1, pViewport);
  vkCmdSetScissor(commandBuffer, 0, 1, pScissor);

  if (renderable.descriptorSet != VK_NULL_HANDLE) {
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            renderable.pipelineLayout, 0, 1,
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
} // Render

#if 0
[[nodiscard]] static tl::expected<absl::FixedArray<VkCommandBuffer>,
                                  std::system_error>
Render(std::vector<Components::Renderable> const& renderables,
       VkRenderPass renderPass) noexcept {
  std::uint32_t const numRenderables = renderables.size();

  VkCommandBufferAllocateInfo commandBufferAI = {};
  commandBufferAI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  commandBufferAI.commandPool = sGraphicsCommandPools[0];
  commandBufferAI.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
  commandBufferAI.commandBufferCount = numRenderables;

  absl::FixedArray<VkCommandBuffer> commandBuffers(numRenderables);
  if (auto result = vkAllocateCommandBuffers(sDevice, &commandBufferAI,
                                             commandBuffers.data());
      result != VK_SUCCESS) {
    return tl::unexpected(std::system_error(make_error_code(result),
                                            "Cannot allocate command buffers"));
  }

  VkCommandBufferInheritanceInfo commandBufferII = {};
  commandBufferII.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
  commandBufferII.renderPass = renderPass;
  commandBufferII.subpass = 0;
  commandBufferII.framebuffer = VK_NULL_HANDLE;

  VkCommandBufferBeginInfo commandBufferBI = {};
  commandBufferBI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  commandBufferBI.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT |
                          VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
  commandBufferBI.pInheritanceInfo = &commandBufferII;

  std::transform(
    renderables.begin(), renderables.end(), commandBuffers.begin(),
    commandBuffers.begin(),
    [&commandBufferBI](Components::Renderable const& renderable,
                       VkCommandBuffer cb) -> VkCommandBuffer {
      Expects(renderable.pipeline != VK_NULL_HANDLE);
      Expects(renderable.pipelineLayout != VK_NULL_HANDLE);

      vkBeginCommandBuffer(cb, &commandBufferBI);
      vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                        renderable.pipeline);

      if (renderable.descriptorSet != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                renderable.pipelineLayout, 0, 1,
                                &renderable.descriptorSet, 0, nullptr);
      }

      if (renderable.vertexBuffer != VK_NULL_HANDLE) {
        vkCmdBindVertexBuffers(cb, 0, 1, &renderable.vertexBuffer,
                               &renderable.vertexBufferBindingOffset);
      }

      if (renderable.indexBuffer != VK_NULL_HANDLE) {
        vkCmdBindIndexBuffer(cb, renderable.indexBuffer,
                             renderable.indexBufferBindingOffset,
                             renderable.indexType);
      }

      if (renderable.numIndices > 0) {
        vkCmdDrawIndexed(cb, renderable.numIndices, renderable.instanceCount,
                         renderable.firstIndex, renderable.vertexOffset,
                         renderable.firstInstance);
      } else {
        vkCmdDraw(cb, renderable.numVertices, renderable.instanceCount,
                  renderable.firstVertex, renderable.firstInstance);
      }

      vkEndCommandBuffer(cb);
      return cb;
    });

  return std::move(commandBuffers);
} // Render
#endif

static void CreateWindow(iris::Control::Window const& windowMessage) noexcept {
  auto const& bg = windowMessage.background_color();

  Window::Options options = Window::Options::kNone;
  if (windowMessage.show_system_decoration()) {
    options |= Window::Options::kDecorated;
  }
  if (windowMessage.is_stereo()) options |= Window::Options::kStereo;
  if (windowMessage.show_ui()) options |= Window::Options::kShowUI;

  if (auto win = CreateWindow(
        windowMessage.name().c_str(),
        wsi::Offset2D{static_cast<std::int16_t>(windowMessage.x()),
                      static_cast<std::int16_t>(windowMessage.y())},
        wsi::Extent2D{static_cast<std::uint16_t>(windowMessage.width()),
                      static_cast<std::uint16_t>(windowMessage.height())},
        {bg.r(), bg.g(), bg.b(), bg.a()}, options, windowMessage.display(),
        sNumWindowFramesBuffered)) {
    Windows().emplace(windowMessage.name(), std::move(*win));
  } else {
    GetLogger()->warn("Createing window failed: {}", win.error().what());
  }
} // CreateWindow

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

  switch (messageSeverity) {
  case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
    if (objNames.empty()) {
      GetLogger()->trace(msg);
    } else {
      GetLogger()->trace("{} Objects: ({})", msg, objNames);
    }
    break;
  case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
    if (objNames.empty()) {
      GetLogger()->info(msg);
    } else {
      GetLogger()->info("{} Objects: ({})", msg, objNames);
    }
    break;
  case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
    if (objNames.empty()) {
      GetLogger()->warn(msg);
    } else {
      GetLogger()->warn("{} Objects: ({})", msg, objNames);
    }
    break;
  case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
    if (objNames.empty()) {
      GetLogger()->error(msg);
    } else {
      GetLogger()->error("{} Objects: ({})", msg, objNames);
    }
    break;
  default:
    GetLogger()->error("Unhandled VkDebugUtilsMessengerSeverityFlagBitsEXT: {}",
                       messageSeverity);
    if (objNames.empty()) {
      GetLogger()->error(msg);
    } else {
      GetLogger()->error("{} Objects: ({})", msg, objNames);
    }
    break;
  }

  GetLogger()->flush();
  return VK_FALSE;
} // DebugUtilsMessengerCallback

} // namespace iris::Renderer

tl::expected<void, std::system_error>
iris::Renderer::Initialize(gsl::czstring<> appName, Options const& options,
                           std::uint32_t appVersion,
                           spdlog::sinks_init_list logSinks) noexcept {
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
  char const* physicalDeviceExtensionNames[] = {
    VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME,
    VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME,
    VK_KHR_MAINTENANCE2_EXTENSION_NAME,
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
#if 0 // FIXME: which GPUs support this?
    VK_KHR_MULTIVIEW_EXTENSION_NAME
#endif
  };

#if PLATFORM_LINUX
  ::setenv(
    "VK_LAYER_PATH",
    (iris::kVulkanSDKDirectory + "/etc/vulkan/explicit_layer.d"s).c_str(), 0);
#endif

  flextVkInit();

  if (auto instance =
        CreateInstance(appName, appVersion, instanceExtensionNames, layerNames,
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
        VK_QUEUE_GRAPHICS_BIT)) {
    sPhysicalDevice = std::move(*physicalDevice);
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(physicalDevice.error());
  }

  if (auto qfi = GetQueueFamilyIndex(sPhysicalDevice, VK_QUEUE_GRAPHICS_BIT)) {
    sGraphicsQueueFamilyIndex = *qfi;
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(qfi.error());
  }

  std::uint32_t numQueues;
  if (auto dn =
        CreateDevice(sPhysicalDevice, physicalDeviceFeatures,
                     physicalDeviceExtensionNames, sGraphicsQueueFamilyIndex)) {
    std::tie(sDevice, numQueues) = *dn;
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(dn.error());
  }

  NameObject(sDevice, VK_OBJECT_TYPE_INSTANCE, sInstance, "sInstance");
  NameObject(sDevice, VK_OBJECT_TYPE_PHYSICAL_DEVICE, sPhysicalDevice,
             "sPhysicalDevice");
  NameObject(sDevice, VK_OBJECT_TYPE_DEVICE, sDevice, "sDevice");

  sGraphicsCommandQueues.resize(numQueues);
  sGraphicsCommandPools.resize(numQueues);
  sGraphicsCommandFences.resize(numQueues);

  VkCommandPoolCreateInfo commandPoolCI = {};
  commandPoolCI.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  commandPoolCI.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  commandPoolCI.queueFamilyIndex = sGraphicsQueueFamilyIndex;

  VkFenceCreateInfo fenceCI = {};
  fenceCI.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

  for (std::uint32_t i = 0; i < numQueues; ++i) {
    vkGetDeviceQueue(sDevice, sGraphicsQueueFamilyIndex, i,
                     &sGraphicsCommandQueues[i]);

    NameObject(sDevice, VK_OBJECT_TYPE_QUEUE, sGraphicsCommandQueues[i],
               fmt::format("sGraphicsCommandQueue[{}]", i).c_str());

    if (auto result = vkCreateCommandPool(sDevice, &commandPoolCI, nullptr,
                                          &sGraphicsCommandPools[i]);
        result != VK_SUCCESS) {
      IRIS_LOG_LEAVE();
      return tl::unexpected(std::system_error(
        make_error_code(result), "Cannot create graphics command pool"));
    }

    NameObject(sDevice, VK_OBJECT_TYPE_COMMAND_POOL, &sGraphicsCommandPools[i],
               fmt::format("sGraphicsCommandPools[{}]", i).c_str());

    if (auto result =
          vkCreateFence(sDevice, &fenceCI, nullptr, &sGraphicsCommandFences[i]);
        result != VK_SUCCESS) {
      IRIS_LOG_LEAVE();
      return tl::unexpected(std::system_error(
        make_error_code(result), "Cannot create graphics submit fence"));
    }

    NameObject(sDevice, VK_OBJECT_TYPE_FENCE, &sGraphicsCommandFences[i],
               fmt::format("sGraphicsCommandFences[{}]", i).c_str());
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
    sSurfaceSampleCount,                     // samples
    VK_ATTACHMENT_LOAD_OP_CLEAR,      // loadOp (color and depth)
    VK_ATTACHMENT_STORE_OP_DONT_CARE, // storeOp (color and depth)
    VK_ATTACHMENT_LOAD_OP_CLEAR,      // stencilLoadOp
    VK_ATTACHMENT_STORE_OP_DONT_CARE, // stencilStoreOp
    VK_IMAGE_LAYOUT_UNDEFINED,        // initialLayout
    VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL // finalLayout
  };

  attachments[sDepthStencilResolveAttachmentIndex] = VkAttachmentDescription{
    0,                               // flags
    sSurfaceDepthStencilFormat,       // format
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

  NameObject(sDevice, VK_OBJECT_TYPE_RENDER_PASS, sRenderPass, "sRenderPass");

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

#if 0
  if (auto error = CreateCommandPools(); error.code()) {
    IRIS_LOG_LEAVE();
    return {error};
  }

  if (auto error = CreateDescriptorPools(); error.code()) {
    IRIS_LOG_LEAVE();
    return {error};
  }

  if (auto error = AllocateCommandBuffers(); error.code()) {
    IRIS_LOG_LEAVE();
    return {error};
  }

  if (auto error = CreateUniformBuffers(); error.code()) {
    IRIS_LOG_LEAVE();
    return {error};
  }

  if (auto error = CreateDescriptorSets(); error.code()) {
    IRIS_LOG_LEAVE();
    return {error};
  }
#endif

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

  NameObject(sDevice, VK_OBJECT_TYPE_SURFACE_KHR, window.surface,
             fmt::format("{}.surface", title).c_str());

  VkBool32 surfaceSupported;
  if (auto result = vkGetPhysicalDeviceSurfaceSupportKHR(
        sPhysicalDevice, sGraphicsQueueFamilyIndex, window.surface,
        &surfaceSupported);
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
  commandPoolCI.queueFamilyIndex = sGraphicsQueueFamilyIndex;

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

    NameObject(sDevice, VK_OBJECT_TYPE_SEMAPHORE, frame.imageAvailable,
               fmt::format("{}.frames[{}].imageAvailable", title, i).c_str());

    if (auto result = vkCreateCommandPool(sDevice, &commandPoolCI, nullptr,
                                          &frame.commandPool);
        result != VK_SUCCESS) {
      IRIS_LOG_LEAVE();
      return tl::unexpected(std::system_error(make_error_code(result),
                                              "Cannot create command pool"));
    }

    NameObject(sDevice, VK_OBJECT_TYPE_COMMAND_POOL, frame.commandPool,
               fmt::format("{}.frames[{}].commandPool", title, i).c_str());

    commandBufferAI.commandPool = frame.commandPool;

    if (auto result = vkAllocateCommandBuffers(sDevice, &commandBufferAI,
                                               &frame.commandBuffer);
        result != VK_SUCCESS) {
      IRIS_LOG_LEAVE();
      return tl::unexpected(std::system_error(
        make_error_code(result), "Cannot allocate command buffer"));
    }

    NameObject(sDevice, VK_OBJECT_TYPE_COMMAND_BUFFER, frame.commandBuffer,
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
  int width, height, bytes_per_pixel;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height, &bytes_per_pixel);

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

  newExtent.height = caps.currentExtent.height == UINT32_MAX
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
  swapchainCI.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
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
        sDevice, sAllocator, sSurfaceDepthStencilFormat, newExtent, 1, 1,
        VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        VK_IMAGE_TILING_OPTIMAL, VMA_MEMORY_USAGE_GPU_ONLY,
        {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1})) {
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

  if (auto iav =
        AllocateImageAndView(sDevice, sAllocator, sSurfaceColorFormat.format,
                             newExtent, 1, 1, sSurfaceSampleCount,
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
        sDevice, sAllocator, sSurfaceDepthStencilFormat, newExtent, 1, 1,
        sSurfaceSampleCount, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        VK_IMAGE_TILING_OPTIMAL, VMA_MEMORY_USAGE_GPU_ONLY,
        {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1})) {
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

  if (auto result = TransitionImage(
        sDevice, sGraphicsCommandPools[0], sGraphicsCommandQueues[0],
        sGraphicsCommandFences[0], newColorTarget, VK_IMAGE_LAYOUT_UNDEFINED,
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
        TransitionImage(sDevice, sGraphicsCommandPools[0],
                        sGraphicsCommandQueues[0], sGraphicsCommandFences[0],
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
  NameObject(sDevice, VK_OBJECT_TYPE_SWAPCHAIN_KHR, window.swapchain,
             fmt::format("{}.swapchain", window.title).c_str());

  std::copy_n(newColorImages.begin(), numSwapchainImages,
              window.colorImages.begin());
  for (auto&& [i, image] : enumerate(window.colorImages)) {
    NameObject(sDevice, VK_OBJECT_TYPE_IMAGE, image,
               fmt::format("{}.colorImages[{}]", window.title, i).c_str());
  }

  std::copy_n(newColorImageViews.begin(), numSwapchainImages,
              window.colorImageViews.begin());
  for (auto&& [i, view] : enumerate(window.colorImageViews)) {
    NameObject(sDevice, VK_OBJECT_TYPE_IMAGE_VIEW, view,
               fmt::format("{}.colorImageViews[{}]", window.title, i).c_str());
  }

  window.depthStencilImage = newDepthStencilImage;
  window.depthStencilImageAllocation = newDepthStencilImageAllocation;
  window.depthStencilImageView = newDepthStencilImageView;
  NameObject(sDevice, VK_OBJECT_TYPE_IMAGE, window.depthStencilImage,
             fmt::format("{}.depthStencilImage", window.title).c_str());
  NameObject(sDevice, VK_OBJECT_TYPE_IMAGE_VIEW, window.depthStencilImageView,
             fmt::format("{}.depthStencilImageView", window.title).c_str());

  window.colorTarget = newColorTarget;
  window.colorTargetAllocation = newColorTargetAllocation;
  window.colorTargetView = newColorTargetView;
  NameObject(sDevice, VK_OBJECT_TYPE_IMAGE, window.colorTarget,
             fmt::format("{}.colorTarget", window.title).c_str());
  NameObject(sDevice, VK_OBJECT_TYPE_IMAGE_VIEW, window.colorTargetView,
             fmt::format("{}.colorTargetView", window.title).c_str());

  window.depthStencilTarget = newDepthStencilTarget;
  window.depthStencilTargetAllocation = newDepthStencilTargetAllocation;
  window.depthStencilTargetView = newDepthStencilTargetView;
  NameObject(sDevice, VK_OBJECT_TYPE_IMAGE, window.depthStencilTarget,
             fmt::format("{}.depthStencilTarget", window.title).c_str());
  NameObject(sDevice, VK_OBJECT_TYPE_IMAGE_VIEW, window.depthStencilTargetView,
             fmt::format("{}.depthStencilTargetView", window.title).c_str());

  for (auto&& [i, frame] : enumerate(window.frames)) {
    frame.framebuffer = newFramebuffers[i];
    NameObject(sDevice, VK_OBJECT_TYPE_FRAMEBUFFER, frame.framebuffer,
               fmt::format("{}.frames[{}].framebuffer", window.title, i).c_str());
  }

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

    io.MousePos = ImVec2(-FLT_MAX, -FLT_MAX);

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

void iris::Renderer::EndFrame(
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
    ImGui::EndFrame();

    // currentFrame is still the previous frame, use that imageAvailable
    // semaphore. vkAcquireNextImageKHR will update frameIndex thereby updating
    // currentFrame (via frameIndex).
    window.imageAcquired = window.currentFrame().imageAvailable;

    if (result = vkAcquireNextImageKHR(sDevice, window.swapchain,
                                            UINT64_MAX, window.imageAcquired,
                                            VK_NULL_HANDLE, &window.frameIndex);
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

    clearValues[sColorTargetAttachmentIndex].color = window.clearColor;

    renderPassBI.framebuffer = frame.framebuffer;
    renderPassBI.renderArea.extent = window.extent;
    renderPassBI.pClearValues = clearValues.data();

    vkCmdSetViewport(frame.commandBuffer, 0, 1, &window.viewport);
    vkCmdSetScissor(frame.commandBuffer, 0, 1, &window.scissor);

    vkCmdBeginRenderPass(frame.commandBuffer, &renderPassBI,
                         VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);

    std::vector<Component::Renderable> renderables = sRenderables();
    for (auto&& renderable : renderables) {
      VkCommandBuffer commandBuffer =
        Render(renderable, &window.viewport, &window.scissor);
      vkCmdExecuteCommands(frame.commandBuffer, 1, &commandBuffer);
    }

    vkCmdExecuteCommands(frame.commandBuffer,
                         gsl::narrow_cast<std::uint32_t>(secondaryCBs.size()),
                         secondaryCBs.data());

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

  if (result = vkQueueSubmit(sGraphicsCommandQueues[0], 1, &submitI,
                             frameFinishedFence);
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

    if (result = vkQueuePresentKHR(sGraphicsCommandQueues[0], &presentI);
        result != VK_SUCCESS) {
      GetLogger()->error("Error presenting swapchains: {}", to_string(result));
    }
  }

  sFrameNum += 1;
  sFrameIndex = sFrameNum % sNumWindowFramesBuffered;
  sInFrame = false;
} // iris::Renderer::EndFrame

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
        //} else if (ext.compare(".gltf") == 0) {
        // sIOContinuations.push(io::LoadGLTF(path_));
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
    IOTask* ioTask = new (tbb::task::allocate_root()) IOTask(path);
    tbb::task::enqueue(*ioTask);
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

  if (!iris::Control::Control::Type_IsValid(controlMessage.type())) {
    GetLogger()->error("Invalid controlMessage message type {}",
                       controlMessage.type());
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(
      Error::kControlMessageInvalid,
      fmt::format("Invalid controlMessage type {}", controlMessage.type())));
  }

  switch (controlMessage.type()) {
  case iris::Control::Control_Type_DISPLAYS:
    for (int i = 0; i < controlMessage.displays().windows_size(); ++i) {
      CreateWindow(controlMessage.displays().windows(i));
    }
    break;
  case iris::Control::Control_Type_WINDOW:
    CreateWindow(controlMessage.window());
    break;
  default:
    GetLogger()->error("Unsupported controlMessage message type {}",
                       controlMessage.type());
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(Error::kControlMessageInvalid,
                        fmt::format("Unsupported controlMessage type {}",
                                    controlMessage.type())));
    break;
  }

  IRIS_LOG_LEAVE();
  return {};
} // iris::Renderer::Control

tl::expected<absl::FixedArray<VkCommandBuffer>, std::system_error>
iris::Renderer::AllocateCommandBuffers(VkCommandBufferLevel level,
                                       std::uint32_t count) noexcept {
  IRIS_LOG_ENTER();
  Expects(sDevice != VK_NULL_HANDLE);
  Expects(count > 0);

  VkCommandBufferAllocateInfo commandBufferAI = {};
  commandBufferAI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  commandBufferAI.commandPool = sGraphicsCommandPools[0];
  commandBufferAI.level = level;
  commandBufferAI.commandBufferCount = count;

  absl::FixedArray<VkCommandBuffer> commandBuffers(count);
  if (auto result = vkAllocateCommandBuffers(sDevice, &commandBufferAI,
                                             commandBuffers.data());
      result != VK_SUCCESS) {
    return tl::unexpected(std::system_error(make_error_code(result),
                                            "Cannot allocate command buffers"));
  }

  IRIS_LOG_LEAVE();
  return commandBuffers;
} // iris::Renderer::AllocateCommandBuffers

#if PLATFORM_COMPILER_GCC
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#endif
#include "glslang/Public/ShaderLang.h"
#include "SPIRV/GlslangToSpv.h"
#include "SPIRV/GLSL.std.450.h"
#if PLATFORM_COMPILER_GCC
#pragma GCC diagnostic pop
#endif

namespace iris::Renderer {

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
                                        VkShaderStageFlagBits stage,
                                        std::string name) noexcept {
  IRIS_LOG_ENTER();
  Expects(sDevice != VK_NULL_HANDLE);
  Expects(source.size() > 0);

  VkShaderModule module{VK_NULL_HANDLE};

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

  if (!name.empty()) {
    NameObject(sDevice, VK_OBJECT_TYPE_SHADER_MODULE, module, name.c_str());
  }

  Ensures(module != VK_NULL_HANDLE);
  IRIS_LOG_LEAVE();
  return module;
} // iris::Renderer::CompileShaderFromSource

tl::expected<std::pair<VkPipelineLayout, VkPipeline>, std::system_error>
iris::Renderer::CreateGraphicsPipeline(
  gsl::span<const VkDescriptorSetLayout> descriptorSetLayouts,
  gsl::span<const VkPushConstantRange> pushConstantRanges,
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
  std::uint32_t renderPassSubpass, std::string name) noexcept {
  IRIS_LOG_ENTER();
  Expects(sDevice != VK_NULL_HANDLE);
  Expects(sRenderPass != VK_NULL_HANDLE);

  VkPipelineLayout layout{VK_NULL_HANDLE};
  VkPipeline pipeline{VK_NULL_HANDLE};

  VkPipelineLayoutCreateInfo pipelineLayoutCI = {};
  pipelineLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutCI.setLayoutCount =
    gsl::narrow_cast<std::uint32_t>(descriptorSetLayouts.size());
  pipelineLayoutCI.pSetLayouts = descriptorSetLayouts.data();
  pipelineLayoutCI.pushConstantRangeCount =
    gsl::narrow_cast<std::uint32_t>(pushConstantRanges.size());
  pipelineLayoutCI.pPushConstantRanges = pushConstantRanges.data();

  if (auto result =
        vkCreatePipelineLayout(sDevice, &pipelineLayoutCI, nullptr, &layout);
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
  graphicsPipelineCI.stageCount = static_cast<uint32_t>(shaderStageCIs.size());
  graphicsPipelineCI.pStages = shaderStageCIs.data();
  graphicsPipelineCI.pVertexInputState = &vertexInputStateCI;
  graphicsPipelineCI.pInputAssemblyState = &inputAssemblyStateCI;
  graphicsPipelineCI.pViewportState = &viewportStateCI;
  graphicsPipelineCI.pRasterizationState = &rasterizationStateCI;
  graphicsPipelineCI.pMultisampleState = &multisampleStateCI;
  graphicsPipelineCI.pDepthStencilState = &depthStencilStateCI;
  graphicsPipelineCI.pColorBlendState = &colorBlendStateCI;
  graphicsPipelineCI.pDynamicState = &dynamicStateCI;
  graphicsPipelineCI.layout = layout;
  graphicsPipelineCI.renderPass = sRenderPass;
  graphicsPipelineCI.subpass = renderPassSubpass;

  if (auto result = vkCreateGraphicsPipelines(sDevice, VK_NULL_HANDLE, 1,
                                              &graphicsPipelineCI, nullptr,
                                              &pipeline);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(make_error_code(result),
                                            "Cannot create graphics pipeline"));
  }

  if (!name.empty()) {
    NameObject(sDevice, VK_OBJECT_TYPE_PIPELINE_LAYOUT, layout,
               (name + ".layout").c_str());
    NameObject(sDevice, VK_OBJECT_TYPE_PIPELINE, pipeline, name.c_str());
  }

  Ensures(layout != VK_NULL_HANDLE);
  Ensures(pipeline != VK_NULL_HANDLE);
  IRIS_LOG_LEAVE();
  return std::make_pair(layout, pipeline);
  } // iris::Renderer::CreateGraphicsPipeline

void iris::Renderer::AddRenderable(Component::Renderable renderable) noexcept {
  sRenderables.push_back(std::move(renderable));
} // AddRenderable
