/*! \file
 * \brief \ref iris::Renderer definition.
 */
#include "renderer/renderer.h"
#include "absl/base/macros.h"
#include "absl/container/fixed_array.h"
#include "absl/container/flat_hash_map.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "config.h"
#include "enumerate.h"
#include "error.h"
#include "protos.h"
#include "renderer/command_buffers.h"
#include "renderer/descriptor_sets.h"
#include "renderer/impl.h"
#include "renderer/io/gltf.h"
#include "renderer/io/json.h"
#include "renderer/io/read_file.h"
#include "renderer/shader.h"
#include "renderer/window.h"
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
#include "wsi/window.h"
#if PLATFORM_WINDOWS
#include "wsi/window_win32.h"
#elif PLATFORM_LINUX
#include "wsi/window_x11.h"
#endif
#include <array>
#include <cstdlib>
#if STD_FS_IS_EXPERIMENTAL
#include <experimental/filesystem>
namespace filesystem = std::experimental::filesystem;
#else
#include <filesystem>
namespace filesystem = std::filesystem;
#endif
#include <memory>
#include <string>
#include <vector>

/////
//
// The logging must be directly defined here instead of including "logging.h".
// This is because we need to define the static logger instance here.
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

#ifndef NDEBUG

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

#else

#define IRIS_LOG_ENTER()
#define IRIS_LOG_LEAVE()

#endif

/////
//
// Here are the definitions from impl.h
//
/////

namespace iris::Renderer {

VkInstance sInstance{VK_NULL_HANDLE};
VkDebugUtilsMessengerEXT sDebugUtilsMessenger{VK_NULL_HANDLE};
VkPhysicalDevice sPhysicalDevice{VK_NULL_HANDLE};
std::uint32_t sGraphicsQueueFamilyIndex{UINT32_MAX};
VkDevice sDevice{VK_NULL_HANDLE};
VkQueue sGraphicsCommandQueue{VK_NULL_HANDLE};
VkFence sFrameComplete{VK_NULL_HANDLE};
VmaAllocator sAllocator{VK_NULL_HANDLE};

VkSurfaceFormatKHR sSurfaceColorFormat{VK_FORMAT_B8G8R8A8_UNORM,
                                       VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
VkFormat sSurfaceDepthStencilFormat{VK_FORMAT_D32_SFLOAT};
VkSampleCountFlagBits sSurfaceSampleCount{VK_SAMPLE_COUNT_4_BIT};
VkPresentModeKHR sSurfacePresentMode{VK_PRESENT_MODE_FIFO_KHR};

std::uint32_t sNumRenderPassAttachments{4};
std::uint32_t sColorTargetAttachmentIndex{0};
std::uint32_t sColorResolveAttachmentIndex{1};
std::uint32_t sDepthStencilTargetAttachmentIndex{2};
std::uint32_t sDepthStencilResolveAttachmentIndex{3};

VkRenderPass sRenderPass{VK_NULL_HANDLE};

/////
//
// Additional static private variables
//
/////

static bool sInitialized{false};
static std::atomic_bool sRunning{false};

static tbb::task_scheduler_init sTaskSchedulerInit{
  tbb::task_scheduler_init::deferred};
static tbb::concurrent_queue<std::function<std::system_error(void)>>
  sIOContinuations;

static std::vector<VkCommandPool> sGraphicsCommandPools;
static std::vector<VkDescriptorPool> sGraphicsDescriptorPools;
static VkSemaphore sImagesReadyForPresent{VK_NULL_HANDLE};
static std::mutex sOneTimeSubmitMutex;
static VkFence sOneTimeSubmitFence{VK_NULL_HANDLE};

static std::uint32_t const sNumCommandBuffers{2};
static absl::FixedArray<VkCommandBuffer> sCommandBuffers(sNumCommandBuffers,
                                                         VK_NULL_HANDLE);
static std::uint32_t sCommandBufferIndex{0};

static absl::flat_hash_map<std::string, iris::Renderer::Window>&
Windows() {
  static absl::flat_hash_map<std::string, iris::Renderer::Window> sWindows;
  return sWindows;
} // Windows

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

/*! \brief Create a Vulkan Instance - \b MUST only be called from
 * \ref Initialize.
 *
 * \see
 * https://www.khronos.org/registry/vulkan/specs/1.1-extensions/html/vkspec.html#initialization-instances
 * \see
 * https://www.khronos.org/registry/vulkan/specs/1.1-extensions/html/vkspec.html#extended-functionality-extensions
 * \see
 * https://www.khronos.org/registry/vulkan/specs/1.1-extensions/html/vkspec.html#extensions
 * \see
 * https://vulkan.lunarg.com/doc/sdk/1.1.82.1/windows/layer_configuration.html
 */
[[nodiscard]] static std::system_error
InitInstance(gsl::czstring<> appName, std::uint32_t appVersion,
             gsl::span<gsl::czstring<>> extensionNames,
             gsl::span<gsl::czstring<>> layerNames, bool reportDebug) noexcept {
  IRIS_LOG_ENTER();
  Expects(sInstance == VK_NULL_HANDLE);

  flextVkInit();

  std::uint32_t instanceVersion;
  vkEnumerateInstanceVersion(&instanceVersion); // can only return VK_SUCCESS

  GetLogger()->debug(
    "Vulkan Instance Version: {}.{}.{}", VK_VERSION_MAJOR(instanceVersion),
    VK_VERSION_MINOR(instanceVersion), VK_VERSION_PATCH(instanceVersion));

  //
  // Enumerate and print out the instance extensions
  //

  // Get the number of instance extension properties.
  std::uint32_t numExtensionProperties;
  if (auto result = vkEnumerateInstanceExtensionProperties(
        nullptr, &numExtensionProperties, nullptr);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return {make_error_code(result),
            "Cannot enumerate instance extension properties"};
  }

  // Get the instance extension properties.
  absl::FixedArray<VkExtensionProperties> extensionProperties(
    numExtensionProperties);
  if (auto result = vkEnumerateInstanceExtensionProperties(
        nullptr, &numExtensionProperties, extensionProperties.data());
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return {make_error_code(result),
            "Cannot enumerate instance extension properties"};
  }

  GetLogger()->debug("Instance Extensions:");
  for (auto&& property : extensionProperties) {
    GetLogger()->debug("  {}:", property.extensionName);
  }

  VkApplicationInfo ai = {};
  ai.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  ai.pApplicationName = appName;
  ai.applicationVersion = appVersion;
  ai.pEngineName = "iris";
  ai.engineVersion =
    VK_MAKE_VERSION(kVersionMajor, kVersionMinor, kVersionPatch);

  VkInstanceCreateInfo ci = {};
  ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  ci.pApplicationInfo = &ai;
  ci.enabledLayerCount = gsl::narrow_cast<std::uint32_t>(layerNames.size());
  ci.ppEnabledLayerNames = layerNames.data();
  ci.enabledExtensionCount =
    gsl::narrow_cast<std::uint32_t>(extensionNames.size());
  ci.ppEnabledExtensionNames = extensionNames.data();

  VkDebugUtilsMessengerCreateInfoEXT dumci = {};
  dumci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
  dumci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                          VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
                          VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                          VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;
  dumci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                      VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                      VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
  dumci.pfnUserCallback = &DebugUtilsMessengerCallback;

  if (reportDebug) ci.pNext = &dumci;

  if (auto result = vkCreateInstance(&ci, nullptr, &sInstance);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return {make_error_code(result), "Cannot create instance"};
  }

  flextVkInitInstance(sInstance); // initialize instance function pointers

  Ensures(sInstance != VK_NULL_HANDLE);
  IRIS_LOG_LEAVE();
  return {Error::kNone};
} // InitInstance

[[nodiscard]] static std::system_error CreateDebugUtilsMessenger() noexcept {
  IRIS_LOG_ENTER();
  Expects(sInstance != VK_NULL_HANDLE);
  Expects(sDebugUtilsMessenger == VK_NULL_HANDLE);

  VkDebugUtilsMessengerCreateInfoEXT dumci = {};
  dumci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
  dumci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                          VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
                          VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                          VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;
  dumci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                      VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                      VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
  dumci.pfnUserCallback = &DebugUtilsMessengerCallback;

  if (auto result = vkCreateDebugUtilsMessengerEXT(sInstance, &dumci, nullptr,
                                                   &sDebugUtilsMessenger);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return {make_error_code(result), "Cannot create debug utils messenger"};
  }

  Ensures(sDebugUtilsMessenger != VK_NULL_HANDLE);
  IRIS_LOG_LEAVE();
  return {Error::kNone};
} // CreateDebugUtilsMessenger

static void DumpPhysicalDevice(VkPhysicalDevice device, std::size_t index,
                               int indentAmount = 0) noexcept {
  IRIS_LOG_ENTER();

  //
  // Get the properties.
  //

  VkPhysicalDeviceMultiviewProperties multiviewProps = {};
  multiviewProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_PROPERTIES;

  VkPhysicalDeviceMaintenance3Properties maint3Props = {};
  maint3Props.sType =
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES;
  maint3Props.pNext = &multiviewProps;

  VkPhysicalDeviceProperties2 physicalDeviceProperties = {};
  physicalDeviceProperties.sType =
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
  physicalDeviceProperties.pNext = &maint3Props;

  vkGetPhysicalDeviceProperties2(device, &physicalDeviceProperties);

  //
  // Get the features.
  //

  VkPhysicalDeviceFeatures2 physicalDeviceFeatures = {};
  physicalDeviceFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;

  vkGetPhysicalDeviceFeatures2(device, &physicalDeviceFeatures);

  //
  // Get the queue family properties.
  //

  // Get the number of physical device queue family properties
  std::uint32_t numQueueFamilyProperties;
  vkGetPhysicalDeviceQueueFamilyProperties2(device, &numQueueFamilyProperties,
                                            nullptr);

  // Get the physical device queue family properties
  absl::FixedArray<VkQueueFamilyProperties2> queueFamilyProperties(
    numQueueFamilyProperties);
  for (auto& property : queueFamilyProperties) {
    property.sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2;
    property.pNext = nullptr;
  }

  vkGetPhysicalDeviceQueueFamilyProperties2(device, &numQueueFamilyProperties,
                                            queueFamilyProperties.data());

  //
  // Get the extension properties.
  //

  // Get the number of physical device extension properties.
  std::uint32_t numExtensionProperties;
  if (auto result = vkEnumerateDeviceExtensionProperties(
        device, nullptr, &numExtensionProperties, nullptr);
      result != VK_SUCCESS) {
    GetLogger()->warn("Cannot enumerate device extension properties: {}",
                      to_string(result));
  }

  // Get the physical device extension properties.
  absl::FixedArray<VkExtensionProperties> extensionProperties(
    numExtensionProperties);
  if (auto result = vkEnumerateDeviceExtensionProperties(
        device, nullptr, &numExtensionProperties, extensionProperties.data());
      result != VK_SUCCESS) {
    GetLogger()->warn("Cannot enumerate device extension properties: {}",
                      to_string(result));
  }

  auto& deviceProps = physicalDeviceProperties.properties;
  auto& features = physicalDeviceFeatures.features;
  std::string indent(indentAmount, ' ');

  GetLogger()->debug("{}Physical Device {} {}", indent, index,
                     deviceProps.deviceName);
  GetLogger()->debug("{}  {} Driver v{}.{}.{} API v{}.{}.{} ", indent,
                     to_string(deviceProps.deviceType),
                     VK_VERSION_MAJOR(deviceProps.driverVersion),
                     VK_VERSION_MINOR(deviceProps.driverVersion),
                     VK_VERSION_PATCH(deviceProps.driverVersion),
                     VK_VERSION_MAJOR(deviceProps.apiVersion),
                     VK_VERSION_MINOR(deviceProps.apiVersion),
                     VK_VERSION_PATCH(deviceProps.apiVersion));

  GetLogger()->debug("{}  Features:", indent);
  GetLogger()->debug("{}    robustBufferAccess: {}", indent,
                     features.robustBufferAccess == VK_TRUE ? "true" : "false");
  GetLogger()->debug("{}    fullDrawIndexUint32: {}", indent,
                     features.fullDrawIndexUint32 == VK_TRUE ? "true"
                                                             : "false");
  GetLogger()->debug("{}    imageCubeArray: {}", indent,
                     features.imageCubeArray == VK_TRUE ? "true" : "false");
  GetLogger()->debug("{}    independentBlend: {}", indent,
                     features.independentBlend == VK_TRUE ? "true" : "false");
  GetLogger()->debug("{}    geometryShader: {}", indent,
                     features.geometryShader == VK_TRUE ? "true" : "false");
  GetLogger()->debug("{}    tessellationShader: {}", indent,
                     features.tessellationShader == VK_TRUE ? "true" : "false");
  GetLogger()->debug("{}    sampleRateShading: {}", indent,
                     features.sampleRateShading == VK_TRUE ? "true" : "false");
  GetLogger()->debug("{}    dualSrcBlend: {}", indent,
                     features.dualSrcBlend == VK_TRUE ? "true" : "false");
  GetLogger()->debug("{}    logicOp: {}", indent,
                     features.logicOp == VK_TRUE ? "true" : "false");
  GetLogger()->debug("{}    multiDrawIndirect: {}", indent,
                     features.multiDrawIndirect == VK_TRUE ? "true" : "false");
  GetLogger()->debug("{}    drawIndirectFirstInstance: {}", indent,
                     features.drawIndirectFirstInstance == VK_TRUE ? "true"
                                                                   : "false");
  GetLogger()->debug("{}    depthClamp: {}", indent,
                     features.depthClamp == VK_TRUE ? "true" : "false");
  GetLogger()->debug("{}    depthBiasClamp: {}", indent,
                     features.depthBiasClamp == VK_TRUE ? "true" : "false");
  GetLogger()->debug("{}    fillModeNonSolid: {}", indent,
                     features.fillModeNonSolid == VK_TRUE ? "true" : "false");
  GetLogger()->debug("{}    depthBounds: {}", indent,
                     features.depthBounds == VK_TRUE ? "true" : "false");
  GetLogger()->debug("{}    wideLines: {}", indent,
                     features.wideLines == VK_TRUE ? "true" : "false");
  GetLogger()->debug("{}    largePoints: {}", indent,
                     features.largePoints == VK_TRUE ? "true" : "false");
  GetLogger()->debug("{}    alphaToOne: {}", indent,
                     features.alphaToOne == VK_TRUE ? "true" : "false");
  GetLogger()->debug("{}    multiViewport: {}", indent,
                     features.multiViewport == VK_TRUE ? "true" : "false");
  GetLogger()->debug("{}    samplerAnisotropy: {}", indent,
                     features.samplerAnisotropy == VK_TRUE ? "true" : "false");
  GetLogger()->debug("{}    textureCompressionETC2: {}", indent,
                     features.textureCompressionETC2 == VK_TRUE ? "true"
                                                                : "false");
  GetLogger()->debug("{}    textureCompressionASTC_LDR: {}", indent,
                     features.textureCompressionASTC_LDR == VK_TRUE ? "true"
                                                                    : "false");
  GetLogger()->debug("{}    textureCompressionBC: {}", indent,
                     features.textureCompressionBC == VK_TRUE ? "true"
                                                              : "false");
  GetLogger()->debug("{}    occlusionQueryPrecise: {}", indent,
                     features.occlusionQueryPrecise == VK_TRUE ? "true"
                                                               : "false");
  GetLogger()->debug("{}    pipelineStatisticsQuery: {}", indent,
                     features.pipelineStatisticsQuery == VK_TRUE ? "true"
                                                                 : "false");
  GetLogger()->debug(
    "{}    vertexPipelineStoresAndAtomics: {}", indent,
    features.vertexPipelineStoresAndAtomics == VK_TRUE ? "true" : "false");
  GetLogger()->debug("{}    fragmentStoresAndAtomics: {}", indent,
                     features.fragmentStoresAndAtomics == VK_TRUE ? "true"
                                                                  : "false");
  GetLogger()->debug("{}    shaderTessellationAndGeometryPointSize: {}", indent,
                     features.shaderTessellationAndGeometryPointSize == VK_TRUE
                       ? "true"
                       : "false");
  GetLogger()->debug("{}    shaderImageGatherExtended: {}", indent,
                     features.shaderImageGatherExtended == VK_TRUE ? "true"
                                                                   : "false");
  GetLogger()->debug(
    "{}    shaderStorageImageExtendedFormats: {}", indent,
    features.shaderStorageImageExtendedFormats == VK_TRUE ? "true" : "false");
  GetLogger()->debug(
    "{}    shaderStorageImageMultisample: {}", indent,
    features.shaderStorageImageMultisample == VK_TRUE ? "true" : "false");
  GetLogger()->debug(
    "{}    shaderStorageImageReadWithoutFormat: {}", indent,
    features.shaderStorageImageReadWithoutFormat == VK_TRUE ? "true" : "false");
  GetLogger()->debug("{}    shaderStorageImageWriteWithoutFormat: {}", indent,
                     features.shaderStorageImageWriteWithoutFormat == VK_TRUE
                       ? "true"
                       : "false");
  GetLogger()->debug(
    "{}    shaderUniformBufferArrayDynamicIndexing: {}", indent,
    features.shaderUniformBufferArrayDynamicIndexing == VK_TRUE ? "true"
                                                                : "false");
  GetLogger()->debug("{}    shaderSampledImageArrayDynamicIndexing: {}", indent,
                     features.shaderSampledImageArrayDynamicIndexing == VK_TRUE
                       ? "true"
                       : "false");
  GetLogger()->debug(
    "{}    shaderStorageBufferArrayDynamicIndexing: {}", indent,
    features.shaderStorageBufferArrayDynamicIndexing == VK_TRUE ? "true"
                                                                : "false");
  GetLogger()->debug("{}    shaderStorageImageArrayDynamicIndexing: {}", indent,
                     features.shaderStorageImageArrayDynamicIndexing == VK_TRUE
                       ? "true"
                       : "false");
  GetLogger()->debug("{}    shaderClipDistance: {}", indent,
                     features.shaderClipDistance == VK_TRUE ? "true" : "false");
  GetLogger()->debug("{}    shaderCullDistance: {}", indent,
                     features.shaderCullDistance == VK_TRUE ? "true" : "false");
  GetLogger()->debug("{}    shaderFloat64: {}", indent,
                     features.shaderFloat64 == VK_TRUE ? "true" : "false");
  GetLogger()->debug("{}    shaderInt64: {}", indent,
                     features.shaderInt64 == VK_TRUE ? "true" : "false");
  GetLogger()->debug("{}    shaderInt16: {}", indent,
                     features.shaderInt16 == VK_TRUE ? "true" : "false");
  GetLogger()->debug("{}    shaderResourceResidency: {}", indent,
                     features.shaderResourceResidency == VK_TRUE ? "true"
                                                                 : "false");
  GetLogger()->debug("{}    shaderResourceMinLod: {}", indent,
                     features.shaderResourceMinLod == VK_TRUE ? "true"
                                                              : "false");
  GetLogger()->debug("{}    sparseBinding: {}", indent,
                     features.sparseBinding == VK_TRUE ? "true" : "false");
  GetLogger()->debug("{}    sparseResidencyBuffer: {}", indent,
                     features.sparseResidencyBuffer == VK_TRUE ? "true"
                                                               : "false");
  GetLogger()->debug("{}    sparseResidencyImage2D: {}", indent,
                     features.sparseResidencyImage2D == VK_TRUE ? "true"
                                                                : "false");
  GetLogger()->debug("{}    sparseResidencyImage3D: {}", indent,
                     features.sparseResidencyImage3D == VK_TRUE ? "true"
                                                                : "false");
  GetLogger()->debug("{}    sparseResidency2Samples: {}", indent,
                     features.sparseResidency2Samples == VK_TRUE ? "true"
                                                                 : "false");
  GetLogger()->debug("{}    sparseResidency4Samples: {}", indent,
                     features.sparseResidency4Samples == VK_TRUE ? "true"
                                                                 : "false");
  GetLogger()->debug("{}    sparseResidency8Samples: {}", indent,
                     features.sparseResidency8Samples == VK_TRUE ? "true"
                                                                 : "false");
  GetLogger()->debug("{}    sparseResidency16Samples: {}", indent,
                     features.sparseResidency16Samples == VK_TRUE ? "true"
                                                                  : "false");
  GetLogger()->debug("{}    sparseResidencyAliased: {}", indent,
                     features.sparseResidencyAliased == VK_TRUE ? "true"
                                                                : "false");
  GetLogger()->debug("{}    variableMultisampleRate: {}", indent,
                     features.variableMultisampleRate == VK_TRUE ? "true"
                                                                 : "false");
  GetLogger()->debug("{}    inheritedQueries: {}", indent,
                     features.inheritedQueries == VK_TRUE ? "true" : "false");

  GetLogger()->debug("{}  Limits:", indent);
  GetLogger()->debug("{}    maxMultiviewViews: {}", indent,
                     multiviewProps.maxMultiviewViewCount);

  GetLogger()->debug("{}  Queue Families:", indent);
  for (std::size_t i = 0; i < queueFamilyProperties.size(); ++i) {
    auto& qfProps = queueFamilyProperties[i].queueFamilyProperties;
    GetLogger()->debug(
      "{}    index: {} count: {} flags: {}", indent, i, qfProps.queueCount,
      to_string(static_cast<VkQueueFlagBits>(qfProps.queueFlags)));
  }

  GetLogger()->debug("{}  Extensions:", indent);
  for (auto&& property : extensionProperties) {
    GetLogger()->debug("{}    {}", indent, property.extensionName);
  }
} // DumpPhysicalDevice

/*! \brief Compare two VkPhysicalDeviceFeatures2 structures.
 *
 * \see
 * https://www.khronos.org/registry/vulkan/specs/1.1-extensions/html/vkspec.html#features-features
 */
[[nodiscard]] static bool
ComparePhysicalDeviceFeatures(VkPhysicalDeviceFeatures2 a,
                              VkPhysicalDeviceFeatures2 b) noexcept {
  bool result = false;
  result |= (a.features.robustBufferAccess == b.features.robustBufferAccess);
  result |= (a.features.fullDrawIndexUint32 == b.features.fullDrawIndexUint32);
  result |= (a.features.imageCubeArray == b.features.imageCubeArray);
  result |= (a.features.independentBlend == b.features.independentBlend);
  result |= (a.features.geometryShader == b.features.geometryShader);
  result |= (a.features.tessellationShader == b.features.tessellationShader);
  result |= (a.features.sampleRateShading == b.features.sampleRateShading);
  result |= (a.features.dualSrcBlend == b.features.dualSrcBlend);
  result |= (a.features.logicOp == b.features.logicOp);
  result |= (a.features.multiDrawIndirect == b.features.multiDrawIndirect);
  result |= (a.features.drawIndirectFirstInstance ==
             b.features.drawIndirectFirstInstance);
  result |= (a.features.depthClamp == b.features.depthClamp);
  result |= (a.features.depthBiasClamp == b.features.depthBiasClamp);
  result |= (a.features.fillModeNonSolid == b.features.fillModeNonSolid);
  result |= (a.features.depthBounds == b.features.depthBounds);
  result |= (a.features.wideLines == b.features.wideLines);
  result |= (a.features.largePoints == b.features.largePoints);
  result |= (a.features.alphaToOne == b.features.alphaToOne);
  result |= (a.features.multiViewport == b.features.multiViewport);
  result |= (a.features.samplerAnisotropy == b.features.samplerAnisotropy);
  result |=
    (a.features.textureCompressionETC2 == b.features.textureCompressionETC2);
  result |= (a.features.textureCompressionASTC_LDR ==
             b.features.textureCompressionASTC_LDR);
  result |=
    (a.features.textureCompressionBC == b.features.textureCompressionBC);
  result |=
    (a.features.occlusionQueryPrecise == b.features.occlusionQueryPrecise);
  result |=
    (a.features.pipelineStatisticsQuery == b.features.pipelineStatisticsQuery);
  result |= (a.features.vertexPipelineStoresAndAtomics ==
             b.features.vertexPipelineStoresAndAtomics);
  result |= (a.features.fragmentStoresAndAtomics ==
             b.features.fragmentStoresAndAtomics);
  result |= (a.features.shaderTessellationAndGeometryPointSize ==
             b.features.shaderTessellationAndGeometryPointSize);
  result |= (a.features.shaderImageGatherExtended ==
             b.features.shaderImageGatherExtended);
  result |= (a.features.shaderStorageImageExtendedFormats ==
             b.features.shaderStorageImageExtendedFormats);
  result |= (a.features.shaderStorageImageMultisample ==
             b.features.shaderStorageImageMultisample);
  result |= (a.features.shaderStorageImageReadWithoutFormat ==
             b.features.shaderStorageImageReadWithoutFormat);
  result |= (a.features.shaderStorageImageWriteWithoutFormat ==
             b.features.shaderStorageImageWriteWithoutFormat);
  result |= (a.features.shaderUniformBufferArrayDynamicIndexing ==
             b.features.shaderUniformBufferArrayDynamicIndexing);
  result |= (a.features.shaderSampledImageArrayDynamicIndexing ==
             b.features.shaderSampledImageArrayDynamicIndexing);
  result |= (a.features.shaderStorageBufferArrayDynamicIndexing ==
             b.features.shaderStorageBufferArrayDynamicIndexing);
  result |= (a.features.shaderStorageImageArrayDynamicIndexing ==
             b.features.shaderStorageImageArrayDynamicIndexing);
  result |= (a.features.shaderClipDistance == b.features.shaderClipDistance);
  result |= (a.features.shaderCullDistance == b.features.shaderCullDistance);
  result |= (a.features.shaderFloat64 == b.features.shaderFloat64);
  result |= (a.features.shaderInt64 == b.features.shaderInt64);
  result |= (a.features.shaderInt16 == b.features.shaderInt16);
  result |=
    (a.features.shaderResourceResidency == b.features.shaderResourceResidency);
  result |=
    (a.features.shaderResourceMinLod == b.features.shaderResourceMinLod);
  result |= (a.features.sparseBinding == b.features.sparseBinding);
  result |=
    (a.features.sparseResidencyBuffer == b.features.sparseResidencyBuffer);
  result |=
    (a.features.sparseResidencyImage2D == b.features.sparseResidencyImage2D);
  result |=
    (a.features.sparseResidencyImage3D == b.features.sparseResidencyImage3D);
  result |=
    (a.features.sparseResidency2Samples == b.features.sparseResidency2Samples);
  result |=
    (a.features.sparseResidency4Samples == b.features.sparseResidency4Samples);
  result |=
    (a.features.sparseResidency8Samples == b.features.sparseResidency8Samples);
  result |= (a.features.sparseResidency16Samples ==
             b.features.sparseResidency16Samples);
  result |=
    (a.features.sparseResidencyAliased == b.features.sparseResidencyAliased);
  result |=
    (a.features.variableMultisampleRate == b.features.variableMultisampleRate);
  result |= (a.features.inheritedQueries == b.features.inheritedQueries);
  return result;
} // ComparePhysicalDeviceFeatures

/*! \brief Check if a specific physical device meets our requirements.
 *
 * \see
 * https://www.khronos.org/registry/vulkan/specs/1.1-extensions/html/vkspec.html#devsandqueues-physical-device-enumeration
 */
[[nodiscard]] static tl::expected<std::uint32_t, std::system_error>
IsPhysicalDeviceGood(VkPhysicalDevice device,
                     VkPhysicalDeviceFeatures2 features,
                     gsl::span<gsl::czstring<>> extensionNames) noexcept {
  IRIS_LOG_ENTER();

  //
  // Get the properties.
  //

  VkPhysicalDeviceMultiviewProperties multiviewProps = {};
  multiviewProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_PROPERTIES;

  VkPhysicalDeviceMaintenance3Properties maint3Props = {};
  maint3Props.sType =
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES;
  maint3Props.pNext = &multiviewProps;

  VkPhysicalDeviceProperties2 physicalDeviceProperties = {};
  physicalDeviceProperties.sType =
    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
  physicalDeviceProperties.pNext = &maint3Props;

  vkGetPhysicalDeviceProperties2(device, &physicalDeviceProperties);

  //
  // Get the features.
  //

  VkPhysicalDeviceFeatures2 physicalDeviceFeatures = {};
  physicalDeviceFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;

  vkGetPhysicalDeviceFeatures2(device, &physicalDeviceFeatures);

  //
  // Get the queue family properties.
  //

  // Get the number of physical device queue family properties
  std::uint32_t numQueueFamilyProperties;
  vkGetPhysicalDeviceQueueFamilyProperties2(device, &numQueueFamilyProperties,
                                            nullptr);

  // Get the physical device queue family properties
  absl::FixedArray<VkQueueFamilyProperties2> queueFamilyProperties(
    numQueueFamilyProperties);
  for (auto& property : queueFamilyProperties) {
    property.sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2;
    property.pNext = nullptr;
  }

  vkGetPhysicalDeviceQueueFamilyProperties2(device, &numQueueFamilyProperties,
                                            queueFamilyProperties.data());

  //
  // Get the extension properties.
  //

  // Get the number of physical device extension properties.
  std::uint32_t numExtensionProperties;
  if (auto result = vkEnumerateDeviceExtensionProperties(
        device, nullptr, &numExtensionProperties, nullptr);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(
      make_error_code(result), "Cannot enumerate device extension properties"));
  }

  // Get the physical device extension properties.
  absl::FixedArray<VkExtensionProperties> extensionProperties(
    numExtensionProperties);
  if (auto result = vkEnumerateDeviceExtensionProperties(
        device, nullptr, &numExtensionProperties, extensionProperties.data());
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(
      make_error_code(result), "Cannot enumerate device extension properties"));
  }

  //
  // Check all queried data to see if this device is good.
  //

  // Check for any required features
  if (!ComparePhysicalDeviceFeatures(physicalDeviceFeatures, features)) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(VulkanResult::kErrorFeatureNotPresent,
                        "Requested feature not supported by device"));
  }

  // Check for a graphics queue
  std::uint32_t graphicsQueueFamilyIndex = UINT32_MAX;

  for (auto [i, props] : enumerate(queueFamilyProperties)) {
    auto&& qfProps = props.queueFamilyProperties;
    if (qfProps.queueCount == 0) continue;
    if (!(qfProps.queueFlags & VK_QUEUE_GRAPHICS_BIT)) continue;

    graphicsQueueFamilyIndex = gsl::narrow_cast<std::uint32_t>(i);
    break;
  }

  if (graphicsQueueFamilyIndex == UINT32_MAX) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(VulkanResult::kErrorFeatureNotPresent,
                        "Graphics queue not supported by device"));
  }

  // Check for each required extension
  for (auto&& required : extensionNames) {
    bool found = false;

    for (auto&& property : extensionProperties) {
      if (std::strcmp(required, property.extensionName) == 0) {
        found = true;
        break;
      }
    }

    if (!found) {
      IRIS_LOG_LEAVE();
      return tl::unexpected(std::system_error(
        VulkanResult::kErrorExtensionNotPresent,
        fmt::format("Extension {} not supported by device", required)));
    }
  }

  // At this point we know all required extensions are present.
  IRIS_LOG_LEAVE();
  return graphicsQueueFamilyIndex;
} // IsPhysicalDeviceGood

static void FindDeviceGroup() {
  IRIS_LOG_ENTER();
  Expects(sInstance != VK_NULL_HANDLE);

  std::uint32_t numPhysicalDeviceGroups;
  if (auto result = vkEnumeratePhysicalDeviceGroups(
        sInstance, &numPhysicalDeviceGroups, nullptr);
      result != VK_SUCCESS) {
    GetLogger()->error("Cannot enumerate physical device groups: {}",
                       to_string(result));
    return;
  }

  absl::FixedArray<VkPhysicalDeviceGroupProperties>
    physicalDeviceGroupProperties(numPhysicalDeviceGroups);
  for (auto&& prop : physicalDeviceGroupProperties) {
    prop.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GROUP_PROPERTIES;
    prop.pNext = nullptr;
  }

  if (auto result =
        vkEnumeratePhysicalDeviceGroups(sInstance, &numPhysicalDeviceGroups,
                                        physicalDeviceGroupProperties.data());
      result != VK_SUCCESS) {
    GetLogger()->error("Cannot enumerate physical device groups: {}",
                       to_string(result));
    return;
  }

  GetLogger()->debug("{} physical device groups", numPhysicalDeviceGroups);
  for (std::uint32_t i = 0; i < numPhysicalDeviceGroups; ++i) {
    auto&& props = physicalDeviceGroupProperties[i];
    GetLogger()->debug("Physical Device Group {}", i);
    GetLogger()->debug("  {} physical devices", props.physicalDeviceCount);
    GetLogger()->debug("  subsetAllocation: {}",
                       props.subsetAllocation == VK_TRUE ? "true" : "false");
    for (std::uint32_t j = 0; j < props.physicalDeviceCount; ++j) {
      DumpPhysicalDevice(props.physicalDevices[j], j, 2);
    }
  }

  IRIS_LOG_LEAVE();
} // FindDeviceGroup

/*! \brief Choose the Vulkan physical device - \b MUST only be called from
 * \ref Initialize.
 *
 * \see
 * https://www.khronos.org/registry/vulkan/specs/1.1-extensions/html/vkspec.html#devsandqueues-physical-device-enumeration
 */
[[nodiscard]] static std::system_error
ChoosePhysicalDevice(VkPhysicalDeviceFeatures2 features,
                     gsl::span<gsl::czstring<>> extensionNames) noexcept {
  IRIS_LOG_ENTER();
  Expects(sInstance != VK_NULL_HANDLE);
  Expects(sPhysicalDevice == VK_NULL_HANDLE);

  // Get the number of physical devices present on the system
  std::uint32_t numPhysicalDevices;
  if (auto result =
        vkEnumeratePhysicalDevices(sInstance, &numPhysicalDevices, nullptr);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return {make_error_code(result), "Cannot enumerate physical devices"};
  }

  // Get the physical devices present on the system
  absl::FixedArray<VkPhysicalDevice> physicalDevices(numPhysicalDevices);
  if (auto result = vkEnumeratePhysicalDevices(sInstance, &numPhysicalDevices,
                                               physicalDevices.data());
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return {make_error_code(result), "Cannot enumerate physical devices"};
  }

  // Iterate and dump every physical device
  GetLogger()->debug("{} physical devices", numPhysicalDevices);
  for (auto [i, physicalDevice] : enumerate(physicalDevices)) {
    DumpPhysicalDevice(physicalDevice, i);
  }

  // Iterate through each physical device to find one that we can use.
  for (auto&& physicalDevice : physicalDevices) {
    if (auto graphicsQFI =
          IsPhysicalDeviceGood(physicalDevice, features, extensionNames)) {
      sPhysicalDevice = physicalDevice;
      sGraphicsQueueFamilyIndex = *graphicsQFI;
      break;
    }
  }

  if (sPhysicalDevice == VK_NULL_HANDLE) {
    IRIS_LOG_LEAVE();
    return {Error::kNoPhysicalDevice, "No suitable physical device found"};
  }

  Ensures(sPhysicalDevice != VK_NULL_HANDLE);
  IRIS_LOG_LEAVE();
  return {Error::kNone};
} // ChoosePhysicalDevice

/*! \brief Create the Vulkan logical device - \b MUST only be called from
 * \ref Initialize.
 *
 * \see
 * https://www.khronos.org/registry/vulkan/specs/1.1-extensions/html/vkspec.html#devsandqueues-devices
 * \see
 * https://www.khronos.org/registry/vulkan/specs/1.1-extensions/html/vkspec.html#devsandqueues-queues
 */
[[nodiscard]] static std::system_error
CreateDeviceAndQueues(VkPhysicalDeviceFeatures2 physicalDeviceFeatures,
                      gsl::span<gsl::czstring<>> extensionNames) noexcept {
  IRIS_LOG_ENTER();
  Expects(sPhysicalDevice != VK_NULL_HANDLE);
  Expects(sDevice == VK_NULL_HANDLE);
  Expects(sGraphicsCommandQueue == VK_NULL_HANDLE);

  // Get all of the queue families again, so that we can get the number of
  // queues to create.

  std::uint32_t numQueueFamilyProperties;
  vkGetPhysicalDeviceQueueFamilyProperties2(sPhysicalDevice,
                                            &numQueueFamilyProperties, nullptr);

  absl::FixedArray<VkQueueFamilyProperties2> queueFamilyProperties(
    numQueueFamilyProperties);
  for (auto& property : queueFamilyProperties) {
    property.sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2;
    property.pNext = nullptr;
  }

  vkGetPhysicalDeviceQueueFamilyProperties2(
    sPhysicalDevice, &numQueueFamilyProperties, queueFamilyProperties.data());

  absl::FixedArray<float> priorities(
    queueFamilyProperties[sGraphicsQueueFamilyIndex]
      .queueFamilyProperties.queueCount);
  std::fill_n(std::begin(priorities), priorities.size(), 1.f);

  VkDeviceQueueCreateInfo qci = {};
  qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  qci.queueFamilyIndex = sGraphicsQueueFamilyIndex;
  qci.queueCount = queueFamilyProperties[sGraphicsQueueFamilyIndex]
                     .queueFamilyProperties.queueCount;
  qci.pQueuePriorities = priorities.data();

  VkDeviceCreateInfo ci = {};
  ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  ci.pNext = &physicalDeviceFeatures;
  ci.queueCreateInfoCount = 1;
  ci.pQueueCreateInfos = &qci;
  ci.enabledExtensionCount =
    gsl::narrow_cast<std::uint32_t>(extensionNames.size());
  ci.ppEnabledExtensionNames = extensionNames.data();

  if (auto result = vkCreateDevice(sPhysicalDevice, &ci, nullptr, &sDevice);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return {make_error_code(result), "Cannot create device"};
  }

  vkGetDeviceQueue(sDevice, sGraphicsQueueFamilyIndex, 0,
                   &sGraphicsCommandQueue);

  Ensures(sDevice != VK_NULL_HANDLE);
  Ensures(sGraphicsCommandQueue != VK_NULL_HANDLE);
  IRIS_LOG_LEAVE();
  return {Error::kNone};
} // CreateDevice

[[nodiscard]] static std::system_error CreateCommandPools() noexcept {
  IRIS_LOG_ENTER();
  Expects(sDevice != VK_NULL_HANDLE);
  Expects(sGraphicsCommandPools.empty());

  VkCommandPoolCreateInfo ci = {};
  ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  ci.queueFamilyIndex = sGraphicsQueueFamilyIndex;

  sGraphicsCommandPools.resize(sTaskSchedulerInit.default_num_threads());
  for (auto&& [i, commandPool] : enumerate(sGraphicsCommandPools)) {
    if (auto result = vkCreateCommandPool(sDevice, &ci, nullptr, &commandPool);
        result != VK_SUCCESS) {
      IRIS_LOG_LEAVE();
      return {make_error_code(result),
              fmt::format("Cannot create command pool {}", i)};
    }

    NameObject(VK_OBJECT_TYPE_COMMAND_POOL, commandPool,
               fmt::format("sGraphicsCommandPools:{}", i).c_str());
  }

  for (auto&& commandPool : sGraphicsCommandPools) {
    Ensures(commandPool != VK_NULL_HANDLE);
  }
  IRIS_LOG_LEAVE();
  return {Error::kNone};
} // CreateCommandPools

[[nodiscard]] static std::system_error CreateDescriptorPools() noexcept {
  IRIS_LOG_ENTER();
  Expects(sDevice != VK_NULL_HANDLE);
  Expects(sGraphicsDescriptorPools.empty());

  absl::FixedArray<VkDescriptorPoolSize> descriptorPoolSizes(11);
  descriptorPoolSizes[0] = { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 };
  descriptorPoolSizes[1] = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 };
  descriptorPoolSizes[2] = { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 };
  descriptorPoolSizes[3] = { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 };
  descriptorPoolSizes[4] = { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 };
  descriptorPoolSizes[5] = { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 };
  descriptorPoolSizes[6] = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 };
  descriptorPoolSizes[7] = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 };
  descriptorPoolSizes[8] = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 };
  descriptorPoolSizes[9] = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 };
  descriptorPoolSizes[10] = { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 };

  VkDescriptorPoolCreateInfo ci = {};
  ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  ci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  ci.maxSets = 1000;
  ci.poolSizeCount = static_cast<uint32_t>(descriptorPoolSizes.size());
  ci.pPoolSizes = descriptorPoolSizes.data();

  sGraphicsDescriptorPools.resize(sTaskSchedulerInit.default_num_threads());
  for (auto&& [i, descriptorPool] : enumerate(sGraphicsDescriptorPools)) {
    if (auto result =
          vkCreateDescriptorPool(sDevice, &ci, nullptr, &descriptorPool);
        result != VK_SUCCESS) {
      IRIS_LOG_LEAVE();
      return {make_error_code(result), "Cannot create descriptor pool"};
    }

    NameObject(VK_OBJECT_TYPE_COMMAND_POOL, descriptorPool,
               fmt::format("sGraphicsDescriptorPools:{}", i).c_str());
  }

  for (auto&& descriptorPool : sGraphicsDescriptorPools) {
    Ensures(descriptorPool != VK_NULL_HANDLE);
  }
  IRIS_LOG_LEAVE();
  return {Error::kNone};
} // CreateDescriptorPools

[[nodiscard]] static std::system_error CreateFencesAndSemaphores() noexcept {
  IRIS_LOG_ENTER();
  Expects(sDevice != VK_NULL_HANDLE);
  Expects(sOneTimeSubmitFence == VK_NULL_HANDLE);
  Expects(sFrameComplete == VK_NULL_HANDLE);
  Expects(sImagesReadyForPresent == VK_NULL_HANDLE);

  VkFenceCreateInfo fci = {};
  fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

  if (auto result = vkCreateFence(sDevice, &fci, nullptr, &sOneTimeSubmitFence);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return {make_error_code(result), "Cannot create fence"};
  }

  NameObject(VK_OBJECT_TYPE_FENCE, sOneTimeSubmitFence, "sOneTimeSubmitFence");

  fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;

  if (auto result = vkCreateFence(sDevice, &fci, nullptr, &sFrameComplete);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return {make_error_code(result), "Cannot create fence"};
  }

  NameObject(VK_OBJECT_TYPE_FENCE, sFrameComplete, "sFrameComplete");

  VkSemaphoreCreateInfo sci = {};
  sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

  if (auto result =
        vkCreateSemaphore(sDevice, &sci, nullptr, &sImagesReadyForPresent);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return {make_error_code(result), "Cannot create semaphore"};
  }

  NameObject(VK_OBJECT_TYPE_SEMAPHORE, sImagesReadyForPresent,
             "sImagesReadyForPresent");

  Ensures(sOneTimeSubmitFence != VK_NULL_HANDLE);
  Ensures(sFrameComplete != VK_NULL_HANDLE);
  Ensures(sImagesReadyForPresent != VK_NULL_HANDLE);
  IRIS_LOG_LEAVE();
  return {Error::kNone};
} // CreateFencesAndSemaphores

[[nodiscard]] static std::system_error CreateAllocator() noexcept {
  IRIS_LOG_ENTER();
  Expects(sPhysicalDevice != VK_NULL_HANDLE);
  Expects(sDevice != VK_NULL_HANDLE);
  Expects(sAllocator == VK_NULL_HANDLE);

  VmaAllocatorCreateInfo allocatorInfo = {};
  allocatorInfo.flags = VMA_ALLOCATOR_CREATE_KHR_DEDICATED_ALLOCATION_BIT;
  allocatorInfo.physicalDevice = sPhysicalDevice;
  allocatorInfo.device = sDevice;

  if (auto result = vmaCreateAllocator(&allocatorInfo, &sAllocator);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return {make_error_code(result), "Cannot create allocator"};
  }

  Ensures(sAllocator != VK_NULL_HANDLE);
  IRIS_LOG_LEAVE();
  return {Error::kNone};
} // CreateAllocator

[[nodiscard]] static std::system_error CreateRenderPass() noexcept {
  IRIS_LOG_ENTER();
  Expects(sDevice != VK_NULL_HANDLE);
  Expects(sRenderPass == VK_NULL_HANDLE);

  // Our render pass has four attachments:
  // 0: color
  // 1: resolve color
  // 2: depth stencil
  // 3: resolve depth stencil
  //
  // The four are needed to support multi-sampling.
  //
  // The color (0) and depth stencil (1) attachments are the multi-sampled
  // attachments that will match up with framebuffers that are rendered into.
  //
  // The resolve (2) attachment is then used for presenting the final image (1).
  absl::FixedArray<VkAttachmentDescription> attachments(
    sNumRenderPassAttachments);

  // The multi-sampled color attachment needs to be cleared on load (loadOp).
  // We don't care what the input layout is (initialLayout) but the final
  // layout must be COLOR_ATTCHMENT_OPTIMAL to allow for resolving.
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

  // The resolve color attachment has a single sample and stores the resolved
  // color. It will be transitioned to PRESENT_SRC_KHR for presentation.
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

  // The multi-sampled depth attachment needs to be cleared on load (loadOp).
  // We don't care what the input layout is (initialLayout) but the final
  // layout must be DEPTH_STENCIL_ATTACHMENT_OPTIMAL to allow for resolving.
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

  // The resolve depth attachment needs has a single sample and stores the
  // resolve depth and stencil.
  // We don't care what the input layout is (initialLayout) but the final
  // layout must be COLOR_ATTACHMENT_OPTIMAL to allow for use as a texture.
  attachments[sDepthStencilResolveAttachmentIndex] = VkAttachmentDescription{
    0,                                       // flags
    sSurfaceDepthStencilFormat,              // format
    VK_SAMPLE_COUNT_1_BIT,                   // samples
    VK_ATTACHMENT_LOAD_OP_DONT_CARE,         // loadOp (color and depth)
    VK_ATTACHMENT_STORE_OP_STORE,            // storeOp (color and depth)
    VK_ATTACHMENT_LOAD_OP_DONT_CARE,         // stencilLoadOp
    VK_ATTACHMENT_STORE_OP_STORE,            // stencilStoreOp
    VK_IMAGE_LAYOUT_UNDEFINED,               // initialLayout
    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL // finalLayout
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
    {{
       VK_SUBPASS_EXTERNAL,                           // srcSubpass
       0,                                             // dstSubpass
       VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,          // srcStageMask
       VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, // dstStageMask
       VK_ACCESS_MEMORY_READ_BIT,                     // srcAccessMask
       VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
         VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, // dstAccessMask
       VK_DEPENDENCY_BY_REGION_BIT             // dependencyFlags
     },
     {
       0,                                             // srcSubpass
       VK_SUBPASS_EXTERNAL,                           // dstSubpass
       VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, // srcStageMask
       VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,          // dstStageMask
       VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
         VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, // srcAccessMask
       VK_ACCESS_MEMORY_READ_BIT,              // dstAccessMask
       VK_DEPENDENCY_BY_REGION_BIT             // dependencyFlags
     }}};

  VkRenderPassCreateInfo rpci = {};
  rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  rpci.attachmentCount = gsl::narrow_cast<std::uint32_t>(attachments.size());
  rpci.pAttachments = attachments.data();
  rpci.subpassCount = 1;
  rpci.pSubpasses = &subpass;
  rpci.dependencyCount = gsl::narrow_cast<std::uint32_t>(dependencies.size());
  rpci.pDependencies = dependencies.data();

  if (auto result = vkCreateRenderPass(sDevice, &rpci, nullptr, &sRenderPass);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return {make_error_code(result), "Cannot create render pass"};
  }

  NameObject(VK_OBJECT_TYPE_RENDER_PASS, sRenderPass, "sRenderPass");

  Ensures(sRenderPass != VK_NULL_HANDLE);
  IRIS_LOG_LEAVE();
  return {Error::kNone};
} // CreateRenderPass

[[nodiscard]] static std::system_error AllocateCommandBuffers() noexcept {
  IRIS_LOG_ENTER();
  Expects(sDevice != VK_NULL_HANDLE);
  Expects(sNumCommandBuffers == sCommandBuffers.size());
  for (auto&& commandBuffer : sCommandBuffers) {
    Expects(commandBuffer == VK_NULL_HANDLE);
  }

  VkCommandBufferAllocateInfo ai = {};
  ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  ai.commandPool = sGraphicsCommandPools[0];
  ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  ai.commandBufferCount = sNumCommandBuffers;

  if (auto result =
        vkAllocateCommandBuffers(sDevice, &ai, sCommandBuffers.data());
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return {make_error_code(result), "Cannot allocate command buffers"};
  }

  for (auto&& commandBuffer : sCommandBuffers) {
    NameObject(VK_OBJECT_TYPE_COMMAND_BUFFER, commandBuffer, "sCommandBuffers");
    Ensures(commandBuffer != VK_NULL_HANDLE);
  }

  IRIS_LOG_LEAVE();
  return {Error::kNone};
} // AllocateCommandBuffers

} // namespace iris::Renderer

std::system_error
iris::Renderer::Initialize(gsl::czstring<> appName, Options const& options,
                           std::uint32_t appVersion,
                           spdlog::sinks_init_list logSinks) noexcept {
  GetLogger(logSinks);
  IRIS_LOG_ENTER();

  if (sInitialized) {
    IRIS_LOG_LEAVE();
    return {Error::kAlreadyInitialized};
  }

  GOOGLE_PROTOBUF_VERIFY_VERSION;

  sTaskSchedulerInit.initialize();
  GetLogger()->debug("Default number of task threads: {}",
                    sTaskSchedulerInit.default_num_threads());

  ////
  // In order to reduce the verbosity of the Vulakn API, initialization occurs
  // over several sub-functions below. Each function is called in-order and
  // assumes the previous functions have all be called.
  ////

  std::vector<gsl::czstring<>> layerNames;
  if ((options & Options::kUseValidationLayers) ==
      Options::kUseValidationLayers) {
    layerNames.push_back("VK_LAYER_LUNARG_standard_validation");
  }

  // These are the extensions that we require from the instance.
  std::vector<gsl::czstring<>> instanceExtensionNames = {
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
  physicalDeviceFeatures.features.multiViewport = VK_TRUE;
  physicalDeviceFeatures.features.pipelineStatisticsQuery = VK_TRUE;
  physicalDeviceFeatures.features.shaderTessellationAndGeometryPointSize = VK_TRUE;
  physicalDeviceFeatures.features.shaderUniformBufferArrayDynamicIndexing = VK_TRUE;
  physicalDeviceFeatures.features.shaderSampledImageArrayDynamicIndexing = VK_TRUE;
  physicalDeviceFeatures.features.shaderStorageBufferArrayDynamicIndexing = VK_TRUE;
  physicalDeviceFeatures.features.shaderStorageImageArrayDynamicIndexing = VK_TRUE;
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
    absl::StrCat(iris::kVulkanSDKDirectory, "/etc/explicit_layer.d").c_str(),
    0);
#endif

  if (auto error =
        InitInstance(appName, appVersion, instanceExtensionNames, layerNames,
                     (options & Options::kReportDebugMessages) ==
                       Options::kReportDebugMessages);
      error.code()) {
    IRIS_LOG_LEAVE();
    return {error};
  }

  if ((options & Options::kReportDebugMessages) ==
      Options::kReportDebugMessages) {
    if (auto error = CreateDebugUtilsMessenger(); error.code()) {
      GetLogger()->warn("Cannot create DebugUtilsMessenger: {}", error.what());
    }
  }

  FindDeviceGroup();

  if (auto error = ChoosePhysicalDevice(physicalDeviceFeatures,
                                        physicalDeviceExtensionNames);
      error.code()) {
    IRIS_LOG_LEAVE();
    return {error};
  }

  if (auto error = CreateDeviceAndQueues(physicalDeviceFeatures,
                                         physicalDeviceExtensionNames);
      error.code()) {
    IRIS_LOG_LEAVE();
    return {error};
  }

  if (auto error = CreateCommandPools(); error.code()) {
    IRIS_LOG_LEAVE();
    return {error};
  }

  if (auto error = CreateDescriptorPools(); error.code()) {
    IRIS_LOG_LEAVE();
    return {error};
  }

  if (auto error = CreateFencesAndSemaphores(); error.code()) {
    IRIS_LOG_LEAVE();
    return {error};
  }

  if (auto error = CreateAllocator(); error.code()) {
    IRIS_LOG_LEAVE();
    return {error};
  }

  if (auto error = CreateRenderPass(); error.code()) {
    IRIS_LOG_LEAVE();
    return {error};
  }

  if (auto error = AllocateCommandBuffers(); error.code()) {
    IRIS_LOG_LEAVE();
    return {error};
  }

  sInitialized = true;
  sRunning = true;

  IRIS_LOG_LEAVE();
  return {Error::kNone};
} // iris::Renderer::Initialize

void iris::Renderer::Shutdown() noexcept {
  IRIS_LOG_ENTER();

  vkQueueWaitIdle(sGraphicsCommandQueue);
  vkDeviceWaitIdle(sDevice);

  Windows().clear();

  vkFreeCommandBuffers(sDevice, sGraphicsCommandPools[0],
                       gsl::narrow_cast<std::uint32_t>(sCommandBuffers.size()),
                       sCommandBuffers.data());

  if (sRenderPass != VK_NULL_HANDLE) {
    vkDestroyRenderPass(sDevice, sRenderPass, nullptr);
  }

  if (sAllocator != VK_NULL_HANDLE) { vmaDestroyAllocator(sAllocator); }

  if (sImagesReadyForPresent != VK_NULL_HANDLE) {
    vkDestroySemaphore(sDevice, sImagesReadyForPresent, nullptr);
  }

  if (sFrameComplete != VK_NULL_HANDLE) {
    vkDestroyFence(sDevice, sFrameComplete, nullptr);
  }

  if (sOneTimeSubmitFence != VK_NULL_HANDLE) {
    vkDestroyFence(sDevice, sOneTimeSubmitFence, nullptr);
  }

  for (auto&& descriptorPool : sGraphicsDescriptorPools) {
    if (descriptorPool != VK_NULL_HANDLE) {
      vkDestroyDescriptorPool(sDevice, descriptorPool, nullptr);
    }
  }

  for (auto&& commandPool : sGraphicsCommandPools) {
    if (commandPool != VK_NULL_HANDLE) {
      vkDestroyCommandPool(sDevice, commandPool, nullptr);
    }
  }

  if (sDevice != VK_NULL_HANDLE) { vkDestroyDevice(sDevice, nullptr); }

  if (sDebugUtilsMessenger != VK_NULL_HANDLE) {
    vkDestroyDebugUtilsMessengerEXT(sInstance, sDebugUtilsMessenger, nullptr);
  }

  if (sInstance != VK_NULL_HANDLE) { vkDestroyInstance(sInstance, nullptr); }

  sTaskSchedulerInit.terminate();

  IRIS_LOG_LEAVE();
}

void iris::Renderer::Terminate() noexcept {
  IRIS_LOG_ENTER();
  sRunning = false;
  IRIS_LOG_LEAVE();
} // iris::Renderer::Terminate

bool iris::Renderer::IsRunning() noexcept {
  return sRunning;
} // iris::Renderer::IsRunning

bool iris::Renderer::BeginFrame() noexcept {
  if (!sInitialized || !sRunning) return false;

  decltype(sIOContinuations)::value_type ioContinuation;
  while (sIOContinuations.try_pop(ioContinuation)) {
    if (auto error = ioContinuation(); error.code()) {
      GetLogger()->error(error.what());
    }
  }

  auto&& windows = Windows();
  if (windows.empty()) return false;

  for (auto&& iter : windows) {
    auto&& window = iter.second;

    if (auto error = window.BeginFrame(); error.code()) {
      GetLogger()->error("Error beginning window frame: {}", error.what());
      return false;
    }
  }

  if (auto result =
        vkWaitForFences(sDevice, 1, &sFrameComplete, VK_TRUE, UINT64_MAX);
      result != VK_SUCCESS) {
    GetLogger()->error("Error waiting on fence: {}", to_string(result));
    return false;
  }

  if (auto result = vkResetFences(sDevice, 1, &sFrameComplete);
      result != VK_SUCCESS) {
    GetLogger()->error("Error resetting fence: {}", to_string(result));
    return false;
  }

  if (auto result = vkResetCommandPool(sDevice, sGraphicsCommandPools[0], 0);
      result != VK_SUCCESS) {
    GetLogger()->error("Error resetting command pool: {}", to_string(result));
    return false;
  }

  return true;
} // iris::Renderer::BeginFrame()

void iris::Renderer::EndFrame() noexcept {
  if (!sInitialized || !sRunning) return;
  auto&& windows = Windows();

  //
  // Acquire images/semaphores from all iris::Window objects
  //

  for (auto&& [title, window] : windows) {
    VkResult result =
      vkAcquireNextImageKHR(sDevice, window.surface.swapchain, UINT64_MAX,
                            window.surface.imageAvailable, VK_NULL_HANDLE,
                            &window.surface.currentImageIndex);

    if (result == VK_SUBOPTIMAL_KHR || result == VK_ERROR_OUT_OF_DATE_KHR) {
      GetLogger()->warn("Swapchains out of date; resizing and re-acquiring");
      auto const extent = window.window.Extent();
      window.surface.Resize({extent.width, extent.height});
      window.resized = false;

      result =
        vkAcquireNextImageKHR(sDevice, window.surface.swapchain, UINT64_MAX,
                              window.surface.imageAvailable, VK_NULL_HANDLE,
                              &window.surface.currentImageIndex);
    }

    if (result != VK_SUCCESS) {
      GetLogger()->error(
        "Renderer::Frame: acquiring next image for {} failed: {}", title,
        to_string(result));
    }
  }

  //
  // Build secondary command buffers
  //
#if 0
  if (sSecondaryCommandBuffers.size() < windows.size()) {
    // Re-allocate secondary command buffers
    if (!sSecondaryCommandBuffers.empty()) {
      vkFreeCommandBuffers(
        sDevice, sGraphicsCommandPool,
        gsl::narrow_cast<std::uint32_t>(sSecondaryCommandBuffers.size()),
        sSecondaryCommandBuffers.data());
      sSecondaryCommandBuffers.clear();
    }

    VkCommandBufferAllocateInfo commandBufferAI = {};
    commandBufferAI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    commandBufferAI.commandPool = sGraphicsCommandPool;
    commandBufferAI.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
    commandBufferAI.commandBufferCount =
      gsl::narrow_cast<std::uint32_t>(windows.size());

    sSecondaryCommandBuffers.resize(windows.size());
    if (auto result = vkAllocateCommandBuffers(sDevice, &commandBufferAI,
                                               sSecondaryCommandBuffers.data());
        result != VK_SUCCESS) {
      GetLogger()->error(
        "Renderer::Frame: allocating secondary command buffers failed: {}",
        to_string(result));
    }
  }

  std::transform(
    std::begin(windows), std::end(windows),
    std::begin(sSecondaryCommandBuffers), std::begin(sSecondaryCommandBuffers),
    [](auto&& titleWindow, auto&& commandBuffer) -> VkCommandBuffer {
      auto&& [title, window] = titleWindow;
      (void)title;

      VkCommandBufferInheritanceInfo inheritanceInfo = {};
      inheritanceInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
      inheritanceInfo.renderPass = sRenderPass;
      inheritanceInfo.framebuffer = window.surface.currentFramebuffer();

      VkCommandBufferBeginInfo beginInfo = {};
      beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
      beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT |
                        VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
      beginInfo.pInheritanceInfo = &inheritanceInfo;

      if (auto result = vkBeginCommandBuffer(commandBuffer, &beginInfo);
          result != VK_SUCCESS) {
        GetLogger()->error(
          "Renderer::Frame: begin secondary command buffer failed: {}",
          to_string(result));
      }

      vkCmdSetViewport(commandBuffer, 0, 1, &window.surface.viewport);
      vkCmdSetScissor(commandBuffer, 0, 1, &window.surface.scissor);

      for (auto&& draw : DrawCommands()) {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          draw.pipeline);

        //vkCmdBindDescriptorSets(
        //  cb, VK_PIPELINE_BIND_POINT_GRAPHICS, ui.pipeline.layout, 0,
        //  gsl::narrow_cast<std::uint32_t>(ui.descriptorSets.sets.size()),
        //  ui.descriptorSets.sets.data(), 0, nullptr);

        VkDeviceSize bindingOffset = 0;
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, draw.vertexBuffer.get(),
                               &bindingOffset);

        if (draw.indexCount > 0) {
          vkCmdBindIndexBuffer(commandBuffer, draw.indexBuffer, 0, draw.indexType);
          vkCmdDrawIndexed(commandBuffer, draw.indexCount, 1, 0, 0, 0);
        } else {
          vkCmdDraw(commandBuffer, draw.vertexCount, 1, 0, 0);
        }
      }

      if (auto result = vkEndCommandBuffer(commandBuffer);
          result != VK_SUCCESS) {
        GetLogger()->error(
          "Renderer::Frame: end secondary command buffer failed: {}",
          to_string(result));
      }

      return commandBuffer;
    });
#endif

  //
  // 1. Record primary command buffer for current frame
  //

  sCommandBufferIndex = (sCommandBufferIndex + 1) % sCommandBuffers.size();
  auto&& cb = sCommandBuffers[sCommandBufferIndex];

  VkCommandBufferBeginInfo cbi = {};
  cbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

  if (auto result = vkBeginCommandBuffer(cb, &cbi); result != VK_SUCCESS) {
    GetLogger()->error("Error beginning command buffer: {}", to_string(result));
  }

  absl::FixedArray<VkClearValue> clearValues(sNumRenderPassAttachments);
  clearValues[sDepthStencilTargetAttachmentIndex].depthStencil = {1.f, 0};

  VkRenderPassBeginInfo rbi = {};
  rbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  rbi.renderPass = sRenderPass;
  rbi.clearValueCount =
    gsl::narrow_cast<std::uint32_t>(sNumRenderPassAttachments);

  //
  // 2. For every window, begin rendering
  //

  std::size_t const numWindows = windows.size();
  absl::FixedArray<VkSemaphore> waitSemaphores(numWindows);
  absl::FixedArray<VkSwapchainKHR> swapchains(numWindows);
  absl::FixedArray<std::uint32_t> imageIndices(numWindows);

  for (auto [i, iter] : enumerate(windows)) {
    auto&& window = iter.second;
    auto&& surface = window.surface;

    waitSemaphores[i] = surface.imageAvailable;
    swapchains[i] = surface.swapchain;
    imageIndices[i] = surface.currentImageIndex;

    clearValues[sColorTargetAttachmentIndex].color = surface.clearColor;
    rbi.renderArea.extent = surface.extent;
    rbi.framebuffer = surface.currentFramebuffer();
    rbi.pClearValues = clearValues.data();

    vkCmdSetViewport(cb, 0, 1, &surface.viewport);
    vkCmdSetScissor(cb, 0, 1, &surface.scissor);

    vkCmdBeginRenderPass(cb, &rbi,
                         VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);

    //
    // 3. Execute secondary command buffers
    //

    //vkCmdExecuteCommands(cb, sSecondaryCommandBuffers.size(),
                         //sSecondaryCommandBuffers.data());

    if (auto wcb = window.EndFrame(rbi.framebuffer)) {
      VkCommandBuffer winCB = *wcb;
      if (winCB != VK_NULL_HANDLE) vkCmdExecuteCommands(cb, 1, &winCB);
    } else {
      GetLogger()->error("Error ending window frame: {}", wcb.error().what());
    }

    //
    // 4. Done rendering
    //

    vkCmdEndRenderPass(cb);
  }

  if (auto result = vkEndCommandBuffer(cb); result != VK_SUCCESS) {
    GetLogger()->error("Error ending command buffer: {}", to_string(result));
  }

  //
  // Submit command buffers to a queue, waiting on all acquired image
  // semaphores and signaling a single frameFinished semaphore
  //

  absl::FixedArray<VkPipelineStageFlags> waitDstStages(
    numWindows, VK_PIPELINE_STAGE_TRANSFER_BIT);

  VkSubmitInfo si = {};
  si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  si.waitSemaphoreCount = gsl::narrow_cast<std::uint32_t>(numWindows);
  si.pWaitSemaphores = waitSemaphores.data();
  si.pWaitDstStageMask = waitDstStages.data();
  si.commandBufferCount = 1;
  si.pCommandBuffers = &cb;
  si.signalSemaphoreCount = 1;
  si.pSignalSemaphores = &sImagesReadyForPresent;

  if (auto result =
        vkQueueSubmit(sGraphicsCommandQueue, 1, &si, sFrameComplete);
      result != VK_SUCCESS) {
    GetLogger()->error("Error submitting command buffer: {}",
                       to_string(result));
  }

  //
  // Present the swapchains to a queue
  //

  absl::FixedArray<VkResult> presentResults(numWindows);

  VkPresentInfoKHR pi = {};
  pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  pi.waitSemaphoreCount = 1;
  pi.pWaitSemaphores = &sImagesReadyForPresent;
  pi.swapchainCount = gsl::narrow_cast<std::uint32_t>(numWindows);
  pi.pSwapchains = swapchains.data();
  pi.pImageIndices = imageIndices.data();
  pi.pResults = presentResults.data();

  if (auto result = vkQueuePresentKHR(sGraphicsCommandQueue, &pi);
      result != VK_SUCCESS) {
    GetLogger()->error("Error presenting swapchains: {}", to_string(result));
  }
} // iris::Renderer::EndFrame

std::error_code
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
    IOTask* ioTask = new (tbb::task::allocate_root()) IOTask(path);
    tbb::task::enqueue(*ioTask);
  } catch (std::exception const& e) {
    GetLogger()->error("Error enqueuing IO task for {}: {}", path.string(),
                       e.what());
    return {Error::kFileLoadFailed};
  }

  IRIS_LOG_LEAVE();
  return {Error::kNone};
} // LoadFile

std::error_code
iris::Renderer::Control(iris::Control::Control const& controlMessage) noexcept {
  IRIS_LOG_ENTER();

  if (!iris::Control::Control::Type_IsValid(controlMessage.type())) {
    GetLogger()->error("Invalid controlMessage message type {}",
                       controlMessage.type());
    IRIS_LOG_LEAVE();
    return Error::kControlMessageInvalid;
  }

  switch (controlMessage.type()) {
  case iris::Control::Control_Type_DISPLAYS:
    for (int i = 0; i < controlMessage.displays().windows_size(); ++i) {
      auto&& windowMessage = controlMessage.displays().windows(i);
      auto const& bg = windowMessage.background_color();

      Window::Options options = Window::Options::kNone;
      if (windowMessage.show_system_decoration()) {
        options |= Window::Options::kDecorated;
      }
      if (windowMessage.is_stereo()) options |= Window::Options::kStereo;

      if (auto win = Window::Create(
            windowMessage.name().c_str(),
            wsi::Offset2D{static_cast<std::int16_t>(windowMessage.x()),
                          static_cast<std::int16_t>(windowMessage.y())},
            wsi::Extent2D{static_cast<std::uint16_t>(windowMessage.width()),
                          static_cast<std::uint16_t>(windowMessage.height())},
            {bg.r(), bg.g(), bg.b(), bg.a()}, options,
            windowMessage.display())) {
        Windows().emplace(windowMessage.name(), std::move(*win));
      }
    }
    break;
  case iris::Control::Control_Type_WINDOW: {
    auto&& windowMessage = controlMessage.window();
    auto const& bg = windowMessage.background_color();

    Window::Options options = Window::Options::kNone;
    if (windowMessage.show_system_decoration()) {
      options |= Window::Options::kDecorated;
    }
    if (windowMessage.is_stereo()) options |= Window::Options::kStereo;

    if (auto win = Window::Create(
          windowMessage.name().c_str(),
          wsi::Offset2D{static_cast<std::int16_t>(windowMessage.x()),
                        static_cast<std::int16_t>(windowMessage.y())},
          wsi::Extent2D{static_cast<std::uint16_t>(windowMessage.width()),
                        static_cast<std::uint16_t>(windowMessage.height())},
          {bg.r(), bg.g(), bg.b(), bg.a()}, options, windowMessage.display())) {
      Windows().emplace(windowMessage.name(), std::move(*win));
    }
  } break;
  default:
    GetLogger()->error("Unsupported controlMessage message type {}",
                       controlMessage.type());
    IRIS_LOG_LEAVE();
    return Error::kControlMessageInvalid;
    break;
  }

  IRIS_LOG_LEAVE();
  return Error::kNone;
} // iris::Renderer::Control

tl::expected<VkCommandBuffer, std::system_error>
iris::Renderer::BeginOneTimeSubmit(VkCommandPool commandPool) noexcept {
  IRIS_LOG_ENTER();
  Expects(sDevice != VK_NULL_HANDLE);
  Expects(commandPool != VK_NULL_HANDLE ||
          sGraphicsCommandPools[0] != VK_NULL_HANDLE);

  if (commandPool == VK_NULL_HANDLE) commandPool = sGraphicsCommandPools[0];
  VkCommandBuffer commandBuffer;

  VkCommandBufferAllocateInfo commandBufferAI = {};
  commandBufferAI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  commandBufferAI.commandPool = commandPool;
  commandBufferAI.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  commandBufferAI.commandBufferCount = 1;

  if (auto result =
        vkAllocateCommandBuffers(sDevice, &commandBufferAI, &commandBuffer);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(make_error_code(result),
                        "Cannot allocate one-time submit command buffer"));
  }

  VkCommandBufferBeginInfo commandBufferBI = {};
  commandBufferBI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  commandBufferBI.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

  if (auto result = vkBeginCommandBuffer(commandBuffer, &commandBufferBI);
      result != VK_SUCCESS) {
    vkFreeCommandBuffers(sDevice, commandPool, 1, &commandBuffer);
    return tl::unexpected(std::system_error(
      make_error_code(result), "Cannot begin one time submit command buffer"));
  }

  IRIS_LOG_LEAVE();
  return commandBuffer;
} // iris::Renderer::BeginOneTimeSubmit

std::system_error
iris::Renderer::EndOneTimeSubmit(VkCommandBuffer commandBuffer,
                                 VkCommandPool commandPool) noexcept {
  std::unique_lock<std::mutex> lock{sOneTimeSubmitMutex};

  IRIS_LOG_ENTER();
  Expects(commandBuffer != VK_NULL_HANDLE);
  Expects(commandPool != VK_NULL_HANDLE ||
          sGraphicsCommandPools[0] != VK_NULL_HANDLE);

  if (commandPool == VK_NULL_HANDLE) commandPool = sGraphicsCommandPools[0];

  VkSubmitInfo submit = {};
  submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &commandBuffer;

  if (auto result = vkEndCommandBuffer(commandBuffer); result != VK_SUCCESS) {
    vkFreeCommandBuffers(sDevice, commandPool, 1, &commandBuffer);
    IRIS_LOG_LEAVE();
    return {make_error_code(result),
            "Cannot end one time submit command buffer"};
  }

  if (auto result =
        vkQueueSubmit(sGraphicsCommandQueue, 1, &submit, sOneTimeSubmitFence);
      result != VK_SUCCESS) {
    vkFreeCommandBuffers(sDevice, commandPool, 1, &commandBuffer);
    IRIS_LOG_LEAVE();
    return {make_error_code(result),
            "Cannot submit one time submit command buffer"};
  }

  if (auto result =
        vkWaitForFences(sDevice, 1, &sOneTimeSubmitFence, VK_TRUE, UINT64_MAX);
      result != VK_SUCCESS) {
    vkFreeCommandBuffers(sDevice, commandPool, 1, &commandBuffer);
    IRIS_LOG_LEAVE();
    return {make_error_code(result), "Cannot wait on one time submit fence"};
  }

  if (auto result = vkResetFences(sDevice, 1, &sOneTimeSubmitFence);
      result != VK_SUCCESS) {
    vkFreeCommandBuffers(sDevice, commandPool, 1, &commandBuffer);
    IRIS_LOG_LEAVE();
    return {make_error_code(result), "Cannot reset one time submit fence"};
  }

  vkFreeCommandBuffers(sDevice, commandPool, 1, &commandBuffer);
  IRIS_LOG_LEAVE();
  return {Error::kNone};
} // iris::Renderer::EndOneTimeSubmit

tl::expected<iris::Renderer::CommandBuffers, std::system_error>
iris::Renderer::AllocateCommandBuffers(std::uint32_t count,
                                       VkCommandBufferLevel level) noexcept {
  return CommandBuffers::Allocate(sGraphicsCommandPools[0], count, level);
} // iris::Renderer::AllocateCommandBuffers

tl::expected<iris::Renderer::DescriptorSets, std::system_error>
iris::Renderer::AllocateDescriptorSets(gsl::span<VkDescriptorSetLayoutBinding> bindings,
                       std::uint32_t numSets, std::string name) noexcept {
  return DescriptorSets::Allocate(sGraphicsDescriptorPools[0], bindings,
                                  numSets, std::move(name));
} // iris::Renderer::AllocateDescriptorSets

