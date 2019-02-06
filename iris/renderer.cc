/*! \file
 * \brief \ref iris::Renderer definition.
 */
#include "absl/container/flat_hash_map.h"
#include "absl/container/inlined_vector.h"
#include "config.h"
#include "enumerate.h"
#include "error.h"
#if PLATFORM_COMPILER_GCC
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#endif
#include "glslang/Public/ShaderLang.h"
#if PLATFORM_COMPILER_GCC
#pragma GCC diagnostic pop
#endif
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
#include "vulkan_support.h"
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
#include <string>
#include <utility>

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

namespace iris::Renderer {

static bool sRunning{false};

static VkInstance sInstance{VK_NULL_HANDLE};
static VkDebugUtilsMessengerEXT sDebugUtilsMessenger{VK_NULL_HANDLE};
static VkPhysicalDevice sPhysicalDevice{VK_NULL_HANDLE};
static VkDevice sDevice{VK_NULL_HANDLE};
static VmaAllocator sAllocator{VK_NULL_HANDLE};

static std::uint32_t sGraphicsQueueFamilyIndex{UINT32_MAX};
static VkQueue sGraphicsCommandQueue{VK_NULL_HANDLE};

static bool sInFrame{false};
static std::uint32_t sFrameNum{0};

static tbb::task_scheduler_init sTaskSchedulerInit{
  tbb::task_scheduler_init::deferred};
static tbb::concurrent_queue<std::function<std::system_error(void)>>
  sIOContinuations{};

template <class T>
void NameObject(VkObjectType objectType, T objectHandle,
                gsl::czstring<> objectName) noexcept {
  VkDebugUtilsObjectNameInfoEXT objectNameInfo = {
    VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT, nullptr, objectType,
    reinterpret_cast<std::uint64_t>(objectHandle), objectName};
  vkSetDebugUtilsObjectNameEXT(sDevice, &objectNameInfo);
} // NameObject

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

/*! \brief Create a Vulkan Instance.
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
[[nodiscard]] tl::expected<VkInstance, std::system_error>
CreateInstance(gsl::czstring<> appName, std::uint32_t appVersion,
               gsl::span<gsl::czstring<>> extensionNames,
               gsl::span<gsl::czstring<>> layerNames,
               bool reportDebug) noexcept {
  IRIS_LOG_ENTER();

  std::uint32_t instanceVersion;
  vkEnumerateInstanceVersion(&instanceVersion); // can only return VK_SUCCESS

  GetLogger()->debug(
    "Vulkan Instance Version: {}.{}.{}", VK_VERSION_MAJOR(instanceVersion),
    VK_VERSION_MINOR(instanceVersion), VK_VERSION_PATCH(instanceVersion));

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

  VkInstance instance;
  if (auto result = vkCreateInstance(&ci, nullptr, &instance);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(make_error_code(result), "Cannot create instance"));
  }

  Ensures(instance != VK_NULL_HANDLE);
  IRIS_LOG_LEAVE();
  return instance;
} // CreateInstance

[[nodiscard]] tl::expected<VkDebugUtilsMessengerEXT, std::system_error>
CreateDebugUtilsMessenger(VkInstance instance) noexcept {
  IRIS_LOG_ENTER();
  Expects(instance != VK_NULL_HANDLE);

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

  VkDebugUtilsMessengerEXT messenger;
  if (auto result =
        vkCreateDebugUtilsMessengerEXT(instance, &dumci, nullptr, &messenger);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(
      make_error_code(result), "Cannot create debug utils messenger"));
  }

  Ensures(messenger != VK_NULL_HANDLE);
  IRIS_LOG_LEAVE();
  return messenger;
} // CreateDebugUtilsMessenger

/*! \brief Compare two VkPhysicalDeviceFeatures2 structures.
 *
 * \see
 * https://www.khronos.org/registry/vulkan/specs/1.1-extensions/html/vkspec.html#features-features
 */
[[nodiscard]] bool
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

[[nodiscard]] tl::expected<std::uint32_t, std::system_error>
GetQueueFamilyIndex(VkPhysicalDevice physicalDevice,
                    VkQueueFlags queueFlags) noexcept {
  IRIS_LOG_ENTER();
  Expects(physicalDevice != VK_NULL_HANDLE);

  // Get the number of physical device queue family properties
  std::uint32_t numQueueFamilyProperties;
  vkGetPhysicalDeviceQueueFamilyProperties2(physicalDevice,
                                            &numQueueFamilyProperties, nullptr);

  // Get the physical device queue family properties
  absl::FixedArray<VkQueueFamilyProperties2> queueFamilyProperties(
    numQueueFamilyProperties);
  for (auto& property : queueFamilyProperties) {
    property.sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2;
    property.pNext = nullptr;
  }

  vkGetPhysicalDeviceQueueFamilyProperties2(
    physicalDevice, &numQueueFamilyProperties, queueFamilyProperties.data());

  for (auto [i, props] : enumerate(queueFamilyProperties)) {
    auto&& qfProps = props.queueFamilyProperties;
    if (qfProps.queueCount == 0) continue;

    if (qfProps.queueFlags & queueFlags) {
      IRIS_LOG_LEAVE();
      return i;
    }
  }

  IRIS_LOG_LEAVE();
  return UINT32_MAX;
} // GetQueueFamilyIndex

/*! \brief Check if a specific physical device meets specified requirements.
 *
 * \see
 * https://www.khronos.org/registry/vulkan/specs/1.1-extensions/html/vkspec.html#devsandqueues-physical-device-enumeration
 */
[[nodiscard]] tl::expected<bool, std::system_error> IsPhysicalDeviceGood(
  VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures2 features,
  gsl::span<gsl::czstring<>> extensionNames, VkQueueFlags queueFlags) noexcept {
  IRIS_LOG_ENTER();
  Expects(physicalDevice != VK_NULL_HANDLE);

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

  vkGetPhysicalDeviceProperties2(physicalDevice, &physicalDeviceProperties);

  //
  // Get the features.
  //

  VkPhysicalDeviceFeatures2 physicalDeviceFeatures = {};
  physicalDeviceFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;

  vkGetPhysicalDeviceFeatures2(physicalDevice, &physicalDeviceFeatures);

  //
  // Get the extension properties.
  //

  // Get the number of physical device extension properties.
  std::uint32_t numExtensionProperties;
  if (auto result = vkEnumerateDeviceExtensionProperties(
        physicalDevice, nullptr, &numExtensionProperties, nullptr);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(
      make_error_code(result),
      "Cannot enumerate physical device extension properties"));
  }

  // Get the physical device extension properties.
  absl::FixedArray<VkExtensionProperties> extensionProperties(
    numExtensionProperties);
  if (auto result = vkEnumerateDeviceExtensionProperties(
        physicalDevice, nullptr, &numExtensionProperties,
        extensionProperties.data());
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(
      make_error_code(result),
      "Cannot enumerate physical device extension properties"));
  }

  std::uint32_t queueFamilyIndex;
  if (auto result = GetQueueFamilyIndex(physicalDevice, queueFlags)) {
    queueFamilyIndex = std::move(*result);
  } else {
    return tl::unexpected(result.error());
  }

  //
  // Check all queried data to see if this device is good.
  //

  // Check for any required features
  if (!ComparePhysicalDeviceFeatures(physicalDeviceFeatures, features)) {
    IRIS_LOG_LEAVE();
    return false;
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
      return false;
    }
  }

  // Check for the queue
  if (queueFamilyIndex == UINT32_MAX) {
    IRIS_LOG_LEAVE();
    return false;
  }

  IRIS_LOG_LEAVE();
  return true;
} // IsPhysicalDeviceGood

/*! \brief Choose the Vulkan physical device.
 *
 * \see
 * https://www.khronos.org/registry/vulkan/specs/1.1-extensions/html/vkspec.html#devsandqueues-physical-device-enumeration
 */
[[nodiscard]] tl::expected<VkPhysicalDevice, std::system_error>
ChoosePhysicalDevice(VkInstance instance, VkPhysicalDeviceFeatures2 features,
                     gsl::span<gsl::czstring<>> extensionNames,
                     VkQueueFlags queueFlags) noexcept {
  IRIS_LOG_ENTER();
  Expects(instance != VK_NULL_HANDLE);

  // Get the number of physical devices present on the system
  std::uint32_t numPhysicalDevices;
  if (auto result =
        vkEnumeratePhysicalDevices(instance, &numPhysicalDevices, nullptr);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(
      make_error_code(result), "Cannot enumerate physical devices"));
  }

  // Get the physical devices present on the system
  absl::FixedArray<VkPhysicalDevice> physicalDevices(numPhysicalDevices);
  if (auto result = vkEnumeratePhysicalDevices(instance, &numPhysicalDevices,
                                               physicalDevices.data());
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(
      make_error_code(result), "Cannot enumerate physical devices"));
  }

  // Iterate through each physical device to find one that we can use.
  for (auto&& physicalDevice : physicalDevices) {
    if (auto good = IsPhysicalDeviceGood(physicalDevice, features,
                                         extensionNames, queueFlags)) {
      Ensures(physicalDevice != VK_NULL_HANDLE);
      IRIS_LOG_LEAVE();
      return physicalDevice;
    }
  }

  IRIS_LOG_LEAVE();
  return tl::unexpected(std::system_error(Error::kNoPhysicalDevice,
                                          "No suitable physical device found"));
} // ChoosePhysicalDevice

/*! \brief Create the Vulkan logical device.
 *
 * \see
 * https://www.khronos.org/registry/vulkan/specs/1.1-extensions/html/vkspec.html#devsandqueues-devices
 * \see
 * https://www.khronos.org/registry/vulkan/specs/1.1-extensions/html/vkspec.html#devsandqueues-queues
 */
[[nodiscard]] tl::expected<VkDevice, std::system_error>
CreateDevice(VkPhysicalDevice physicalDevice,
             VkPhysicalDeviceFeatures2 physicalDeviceFeatures,
             gsl::span<gsl::czstring<>> extensionNames,
             std::uint32_t queueFamilyIndex) noexcept {
  IRIS_LOG_ENTER();
  Expects(physicalDevice != VK_NULL_HANDLE);

  // Get all of the queue families again, so that we can get the number of
  // queues to create.

  std::uint32_t numQueueFamilyProperties;
  vkGetPhysicalDeviceQueueFamilyProperties2(physicalDevice,
                                            &numQueueFamilyProperties, nullptr);

  absl::FixedArray<VkQueueFamilyProperties2> queueFamilyProperties(
    numQueueFamilyProperties);
  for (auto& property : queueFamilyProperties) {
    property.sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2;
    property.pNext = nullptr;
  }

  vkGetPhysicalDeviceQueueFamilyProperties2(
    physicalDevice, &numQueueFamilyProperties, queueFamilyProperties.data());

  absl::FixedArray<float> priorities(
    queueFamilyProperties[queueFamilyIndex].queueFamilyProperties.queueCount);
  std::fill_n(std::begin(priorities), priorities.size(), 1.f);

  VkDeviceQueueCreateInfo qci = {};
  qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  qci.queueFamilyIndex = queueFamilyIndex;
  qci.queueCount =
    queueFamilyProperties[queueFamilyIndex].queueFamilyProperties.queueCount;
  qci.pQueuePriorities = priorities.data();

  VkDeviceCreateInfo ci = {};
  ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  ci.pNext = &physicalDeviceFeatures;
  ci.queueCreateInfoCount = 1;
  ci.pQueueCreateInfos = &qci;
  ci.enabledExtensionCount =
    gsl::narrow_cast<std::uint32_t>(extensionNames.size());
  ci.ppEnabledExtensionNames = extensionNames.data();

  VkDevice device;
  if (auto result = vkCreateDevice(physicalDevice, &ci, nullptr, &device);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(make_error_code(result), "Cannot create device"));
  }

  Ensures(device != VK_NULL_HANDLE);
  IRIS_LOG_LEAVE();
  return device;
} // CreateDevice

[[nodiscard]] tl::expected<VmaAllocator, std::system_error>
CreateAllocator(VkPhysicalDevice physicalDevice, VkDevice device) noexcept {
  IRIS_LOG_ENTER();
  Expects(physicalDevice != VK_NULL_HANDLE);
  Expects(device != VK_NULL_HANDLE);

  VmaAllocatorCreateInfo allocatorInfo = {};
  allocatorInfo.flags = VMA_ALLOCATOR_CREATE_KHR_DEDICATED_ALLOCATION_BIT;
  allocatorInfo.physicalDevice = physicalDevice;
  allocatorInfo.device = device;

  VmaAllocator allocator;
  if (auto result = vmaCreateAllocator(&allocatorInfo, &allocator);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(make_error_code(result), "Cannot create allocator"));
  }

  Ensures(allocator != VK_NULL_HANDLE);
  IRIS_LOG_LEAVE();
  return allocator;
} // CreateAllocator

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
    absl::StrCat(iris::kVulkanSDKDirectory, "/etc/explicit_layer.d").c_str(),
    0);
#endif

  flextVkInit();

  if (auto instance =
        CreateInstance(appName, appVersion, instanceExtensionNames, layerNames,
                       (options & Options::kReportDebugMessages) ==
                         Options::kReportDebugMessages)) {
    sInstance = std::move(*instance);
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(instance.error());
  }

  flextVkInitInstance(sInstance); // initialize instance function pointers

  if ((options & Options::kReportDebugMessages) ==
      Options::kReportDebugMessages) {
    if (auto messenger = CreateDebugUtilsMessenger(sInstance)) {
      sDebugUtilsMessenger = std::move(*messenger);
    } else {
      GetLogger()->warn("Cannot create DebugUtilsMessenger: {}",
                        messenger.error().what());
    }
  }

  // FindDeviceGroup();

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

  if (auto device =
        CreateDevice(sPhysicalDevice, physicalDeviceFeatures,
                     physicalDeviceExtensionNames, sGraphicsQueueFamilyIndex)) {
    sDevice = std::move(*device);
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(device.error());
  }

  vkGetDeviceQueue(sDevice, sGraphicsQueueFamilyIndex, 0,
                   &sGraphicsCommandQueue);

  if (auto allocator = CreateAllocator(sPhysicalDevice, sDevice)) {
    sAllocator = std::move(*allocator);
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(allocator.error());
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

  if (auto error = CreateFencesAndSemaphores(); error.code()) {
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
  sRunning = false;
} // iris::Renderer::Terminate

tl::expected<iris::Window, std::exception> iris::Renderer::CreateWindow(
  gsl::czstring<> title, wsi::Offset2D offset, wsi::Extent2D extent,
  glm::vec4 const& clearColor [[maybe_unused]], Window::Options const& options,
  int display, std::uint32_t numFrames) noexcept {
  IRIS_LOG_ENTER();
  Expects(sInstance != VK_NULL_HANDLE);
  Expects(sPhysicalDevice != VK_NULL_HANDLE);
  Expects(sDevice != VK_NULL_HANDLE);

  Window window(numFrames);
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
  std::tie(sci.connection, sci.window) =
    window.platformWindow.NativeHandle()

      if (auto result =
            vkCreateXcbSurfaceKHR(instance, &sci, nullptr, &window.surface);
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

  window.uiContext.reset(ImGui::CreateContext());
  ImGui::SetCurrentContext(window.uiContext.get());
  ImGui::StyleColorsDark();

  ImGuiIO& io = ImGui::GetIO();

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

  window.platformWindow.Show();
  window.platformWindow.OnResize(
    std::bind(&Window::Resize, &window, std::placeholders::_1));
  window.platformWindow.OnClose(std::bind(&Window::Close, &window));

  IRIS_LOG_LEAVE();
  return std::move(window);
} // iris::Renderer::CreateWindow

void iris::Renderer::BeginFrame() noexcept {
  Expects(sRunning);
  Expects(!sInFrame);

  auto currentTime = std::chrono::steady_clock::now();
  // auto const frameDelta =
  // std::chrono::duration<float>(currentTime - sPreviousFrameTime).count();
  // sPreviousFrameTime = currentTime;

  decltype(sIOContinuations)::value_type ioContinuation;
  while (sIOContinuations.try_pop(ioContinuation)) {
    if (auto error = ioContinuation(); error.code()) {
      GetLogger()->error(error.what());
    }
  }

  sInFrame = true;
} // iris::Renderer::BeginFrame()

void iris::Renderer::EndFrame() noexcept {
  Expects(sRunning);
  Expects(sInFrame);

  sFrameNum += 1;
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

    //
    // FIXME: DRY
    //

  case iris::Control::Control_Type_DISPLAYS:
#if 0
    for (int i = 0; i < controlMessage.displays().windows_size(); ++i) {
      auto&& windowMessage = controlMessage.displays().windows(i);
      auto const& bg = windowMessage.background_color();

      Window::Options options = Window::Options::kNone;
      if (windowMessage.show_system_decoration()) {
        options |= Window::Options::kDecorated;
      }
      if (windowMessage.is_stereo()) options |= Window::Options::kStereo;
      if (windowMessage.show_ui()) options |= Window::Options::kShowUI;

      if (auto win = Window::Create(
            windowMessage.name().c_str(),
            wsi::Offset2D{static_cast<std::int16_t>(windowMessage.x()),
                          static_cast<std::int16_t>(windowMessage.y())},
            wsi::Extent2D{static_cast<std::uint16_t>(windowMessage.width()),
                          static_cast<std::uint16_t>(windowMessage.height())},
            {bg.r(), bg.g(), bg.b(), bg.a()}, options,
            windowMessage.display())) {
        Windows().emplace(windowMessage.name(), std::move(*win));
      } else {
        GetLogger()->warn("Createing window failed: {}", win.error().what());
      }
    }
#endif
    break;
  case iris::Control::Control_Type_WINDOW: {
#if 0
    auto&& windowMessage = controlMessage.window();
    auto const& bg = windowMessage.background_color();

    Window::Options options = Window::Options::kNone;
    if (windowMessage.show_system_decoration()) {
      options |= Window::Options::kDecorated;
    }
    if (windowMessage.is_stereo()) options |= Window::Options::kStereo;
    if (windowMessage.show_ui()) options |= Window::Options::kShowUI;

    if (auto win = Window::Create(
          windowMessage.name().c_str(),
          wsi::Offset2D{static_cast<std::int16_t>(windowMessage.x()),
                        static_cast<std::int16_t>(windowMessage.y())},
          wsi::Extent2D{static_cast<std::uint16_t>(windowMessage.width()),
                        static_cast<std::uint16_t>(windowMessage.height())},
          {bg.r(), bg.g(), bg.b(), bg.a()}, options, windowMessage.display())) {
      Windows().emplace(windowMessage.name(), std::move(*win));
    } else {
      GetLogger()->warn("Creating window failed: {}", win.error().what());
    }
#endif
  } break;
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
