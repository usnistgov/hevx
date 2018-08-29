/*! \file
 * \brief \ref iris::Renderer definition.
 */
#include "renderer.h"
#include "absl/base/macros.h"
#include "absl/container/fixed_array.h"
#include "absl/strings/str_cat.h"
#include "config.h"
#include "error.h"
#include "helpers.h"
#include "spdlog/spdlog.h"
#include "tl/expected.hpp"
#include "vulkan_result.h"
#include <cstdlib>

namespace iris::Renderer {

static spdlog::logger* sGetLogger() noexcept {
  static std::shared_ptr<spdlog::logger> sLogger = spdlog::get("iris");
  return sLogger.get();
}

static VkInstance sInstance{VK_NULL_HANDLE};
static VkDebugReportCallbackEXT sDebugReportCallback{VK_NULL_HANDLE};
static VkPhysicalDevice sPhysicalDevice{VK_NULL_HANDLE};
static std::uint32_t sGraphicsQueueFamilyIndex;
static bool sInitialized{false};

#ifndef NDEBUG
/*! \brief Callback for Vulkan Debug Reporting.
 *
 * \see
 * https://www.khronos.org/registry/vulkan/specs/1.0-extensions/html/vkspec.html#debugging-debug-report-callbacks
 */
static VKAPI_ATTR VkBool32 VKAPI_CALL DebugReportCallback(
  VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT /*objectType*/,
  std::uint64_t /*object*/, std::size_t /*location*/,
  std::int32_t /*messageCode*/, char const* pLayerPrefix, char const* pMessage,
  void* /*pUserData*/) {
  if (flags & VK_DEBUG_REPORT_INFORMATION_BIT_EXT) {
    sGetLogger()->info("{}: {}", pLayerPrefix, pMessage);
  } else if (flags & VK_DEBUG_REPORT_WARNING_BIT_EXT) {
    sGetLogger()->warn("{}: {}", pLayerPrefix, pMessage);
  } else if (flags & VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT) {
    sGetLogger()->error("{}: {}", pLayerPrefix, pMessage);
  } else if (flags & VK_DEBUG_REPORT_ERROR_BIT_EXT) {
    sGetLogger()->error("{}: {}", pLayerPrefix, pMessage);
  } else if (flags & VK_DEBUG_REPORT_DEBUG_BIT_EXT) {
    sGetLogger()->debug("{}: {}", pLayerPrefix, pMessage);
  }
  return VK_FALSE;
} // DebugReportCallback
#endif

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
static std::error_code InitInstance(gsl::not_null<gsl::czstring<>> appName,
                                    std::uint32_t appVersion) noexcept {
  IRIS_LOG_ENTER(sGetLogger());
  VkResult result;

  uint32_t instanceVersion;
  vkEnumerateInstanceVersion(&instanceVersion); // can only return VK_SUCCESS

  sGetLogger()->debug(
    "Vulkan Instance Version: {}.{}.{}", VK_VERSION_MAJOR(instanceVersion),
    VK_VERSION_MINOR(instanceVersion), VK_VERSION_PATCH(instanceVersion));

  //
  // Enumerate and print out the instance extensions
  //

  // Get the number of instance extension properties.
  uint32_t numExtensionProperties;
  result = vkEnumerateInstanceExtensionProperties(
    nullptr, &numExtensionProperties, nullptr);
  if (result != VK_SUCCESS) {
    sGetLogger()->error("Cannot enumerate instance extension properties: {}",
                        to_string(result));
    return make_error_code(result);
  }

  // Get the instance extension properties.
  absl::FixedArray<VkExtensionProperties> extensionProperties(
    numExtensionProperties);
  result = vkEnumerateInstanceExtensionProperties(
    nullptr, &numExtensionProperties, extensionProperties.data());
  if (result != VK_SUCCESS) {
    sGetLogger()->error("Cannot enumerate instance extension properties: {}",
                        to_string(result));
    return make_error_code(result);
  }

  sGetLogger()->debug("Instance Extensions:");
  for (auto&& property : extensionProperties) {
    sGetLogger()->debug("  {}:", property.extensionName);
  }

  // validation layers do add overhead, so only use them in Debug configs.
#ifndef NDEBUG
  char const* layerNames[] = {"VK_LAYER_LUNARG_standard_validation"};
#endif

  // These are the extensions that we require from the instance.
  // We are not going to enumerate the extensions and check for each one,
  // instead we're going to just specify those we need and let the instance
  // creation fail if an extension was not found.
  char const* extensionNames[] = {
    VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
    VK_KHR_SURFACE_EXTENSION_NAME, // surfaces are necessary for graphics
    VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME,
#ifdef VK_USE_PLATFORM_XLIB_KHR // we also need the platform-specific surface
    VK_KHR_XLIB_SURFACE_EXTENSION_NAME,
#endif
#ifndef NDEBUG
    VK_EXT_DEBUG_REPORT_EXTENSION_NAME
#endif
  };

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
#ifndef NDEBUG
  ci.enabledLayerCount = gsl::narrow_cast<uint32_t>(ABSL_ARRAYSIZE(layerNames));
  ci.ppEnabledLayerNames = layerNames;
#endif
  ci.enabledExtensionCount =
    gsl::narrow_cast<uint32_t>(ABSL_ARRAYSIZE(extensionNames));
  ci.ppEnabledExtensionNames = extensionNames;

#ifndef NDEBUG
  VkDebugReportCallbackCreateInfoEXT drcci = {};
  drcci.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
  drcci.flags = VK_DEBUG_REPORT_INFORMATION_BIT_EXT |
                VK_DEBUG_REPORT_WARNING_BIT_EXT |
                VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT |
                VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_DEBUG_BIT_EXT;
  drcci.pfnCallback = &DebugReportCallback;
  ci.pNext = &drcci;
#endif

  result = vkCreateInstance(&ci, nullptr, &sInstance);
  if (result != VK_SUCCESS) {
    sGetLogger()->error("Cannot create instance: {}", to_string(result));
    IRIS_LOG_LEAVE(sGetLogger());
    return make_error_code(result);
  }

  sGetLogger()->debug("Instance: {}", static_cast<void*>(sInstance));
  IRIS_LOG_LEAVE(sGetLogger());
  return VulkanResult::kSuccess;
} // InitInstance

/*! \brief Create the callback for Vulkan Debug Reporting - \b MUST only be
 * called from \ref Initialize.
 *
 * \see
 * https://www.khronos.org/registry/vulkan/specs/1.0-extensions/html/vkspec.html#debugging-debug-report-callbacks
 */
static std::error_code CreateDebugReportCallback() noexcept {
#ifndef NDEBUG
  IRIS_LOG_ENTER(sGetLogger());
  VkResult result;

  VkDebugReportCallbackCreateInfoEXT drcci = {};
  drcci.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
  drcci.flags = VK_DEBUG_REPORT_INFORMATION_BIT_EXT |
                VK_DEBUG_REPORT_WARNING_BIT_EXT |
                VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT |
                VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_DEBUG_BIT_EXT;
  drcci.pfnCallback = &DebugReportCallback;

  result = vkCreateDebugReportCallbackEXT(sInstance, &drcci, nullptr,
                                          &sDebugReportCallback);
  if (result != VK_SUCCESS) {
    sGetLogger()->warn("Cannot create debug report callback: {}",
                       to_string(result));
  }

  sGetLogger()->debug("Debug Report Callback: {}",
                      static_cast<void*>(sDebugReportCallback));
  IRIS_LOG_LEAVE(sGetLogger());
#endif

  return VulkanResult::kSuccess;
} // CreateDebugReportCallback

static void DumpPhysicalDevice(
  VkPhysicalDeviceProperties2 physicalDeviceProperties,
  absl::FixedArray<VkQueueFamilyProperties2> queueFamilyProperties,
  absl::FixedArray<VkExtensionProperties> extensionProperties) {
  auto& deviceProps = physicalDeviceProperties.properties;
  auto& maint3Props =
    *reinterpret_cast<VkPhysicalDeviceMaintenance3Properties*>(
      physicalDeviceProperties.pNext);
  auto& multiviewProps =
    *reinterpret_cast<VkPhysicalDeviceMultiviewProperties*>(maint3Props.pNext);

  sGetLogger()->debug("Physical Device {} - Type: {} maxMultiviewViews: {}",
                      deviceProps.deviceName, to_string(deviceProps.deviceType),
                      multiviewProps.maxMultiviewViewCount);

  sGetLogger()->debug("  Queue Families:");
  for (std::size_t i = 0; i < queueFamilyProperties.size(); ++i) {
    auto& qfProps = queueFamilyProperties[i].queueFamilyProperties;
    sGetLogger()->debug("    index: {} count: {} flags: {}", i,
                        qfProps.queueCount, to_string(qfProps.queueFlags));
  }

  sGetLogger()->debug("  Extensions:");
  for (auto&& property : extensionProperties) {
    sGetLogger()->debug("    {}:", property.extensionName);
  }
} // DumpPhysicalDevice

/*! \brief Check if a specific physical device meets our requirements.
 *
 * \see
 * https://www.khronos.org/registry/vulkan/specs/1.1-extensions/html/vkspec.html#devsandqueues-physical-device-enumeration
 */
static tl::expected<std::uint32_t, std::error_code>
IsPhysicalDeviceGood(VkPhysicalDevice device,
                     gsl::span<gsl::czstring<>> extensionNames) noexcept {
  IRIS_LOG_ENTER(sGetLogger());
  VkResult result;

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
  //auto& deviceProps = physicalDeviceProperties.properties;

  //
  // Get the features.
  //

  VkPhysicalDeviceFeatures2 physicalDeviceFeatures = {};
  physicalDeviceFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;

  vkGetPhysicalDeviceFeatures2(device, &physicalDeviceFeatures);
  auto& deviceFeatures = physicalDeviceFeatures.features;

  //
  // Get the queue family properties.
  //

  // Get the number of physical device queue family properties
  uint32_t numQueueFamilyProperties;
  vkGetPhysicalDeviceQueueFamilyProperties2(device, &numQueueFamilyProperties,
                                            nullptr);

  // Get the physical device queue family properties
  absl::FixedArray<VkQueueFamilyProperties2> queueFamilyProperties(
    numQueueFamilyProperties);
  for (auto& property : queueFamilyProperties) {
    property.sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2;
  }

  vkGetPhysicalDeviceQueueFamilyProperties2(device, &numQueueFamilyProperties,
                                            queueFamilyProperties.data());

  //
  // Get the extension properties.
  //

  // Get the number of physical device extension properties.
  uint32_t numExtensionProperties;
  result = vkEnumerateDeviceExtensionProperties(
    device, nullptr, &numExtensionProperties, nullptr);
  if (result != VK_SUCCESS) {
    sGetLogger()->warn("Cannot enumerate device extension properties: {}",
                       to_string(result));
    return tl::unexpected(make_error_code(result));
  }

  // Get the physical device extension properties.
  absl::FixedArray<VkExtensionProperties> extensionProperties(
    numExtensionProperties);
  result = vkEnumerateDeviceExtensionProperties(
    device, nullptr, &numExtensionProperties, extensionProperties.data());
  if (result != VK_SUCCESS) {
    sGetLogger()->warn("Cannot enumerate device extension properties: {}",
                       to_string(result));
    return tl::unexpected(make_error_code(result));
  }

  DumpPhysicalDevice(physicalDeviceProperties, queueFamilyProperties,
                     extensionProperties);

  //
  // Check all queried data to see if this device is good.
  //

  // Check for any required features
  // NOTE: these should match what's requested below in CreateDevice
  if (deviceFeatures.fullDrawIndexUint32 == VK_FALSE) {
    sGetLogger()->debug("No fullDrawIndexUint32 supported by device {}",
                        static_cast<void*>(device));
    return tl::unexpected(VulkanResult::kErrorFeatureNotPresent);
  }

  if (deviceFeatures.fillModeNonSolid == VK_FALSE) {
    sGetLogger()->debug("No fillModeNonSolid supported by device {}",
                        static_cast<void*>(device));
    return tl::unexpected(VulkanResult::kErrorFeatureNotPresent);
  }

  if (deviceFeatures.pipelineStatisticsQuery == VK_FALSE) {
    sGetLogger()->debug("No pipelineStatisticsQuery supported by device {}",
                        static_cast<void*>(device));
    return tl::unexpected(VulkanResult::kErrorFeatureNotPresent);
  }

  // Check for a graphics queue
  std::uint32_t graphicsQueueFamilyIndex = UINT32_MAX;

  for (std::size_t i = 0; i < queueFamilyProperties.size(); ++i) {
    auto&& qfProps = queueFamilyProperties[i].queueFamilyProperties;
    if (qfProps.queueCount == 0) continue;
    if (!(qfProps.queueFlags & VK_QUEUE_GRAPHICS_BIT)) continue;
    graphicsQueueFamilyIndex = gsl::narrow_cast<std::uint32_t>(i);

    break;
  }

  if (graphicsQueueFamilyIndex == UINT32_MAX) {
    sGetLogger()->debug("No graphics queue supported by device {}",
                        static_cast<void*>(device));
    return tl::unexpected(VulkanResult::kErrorFeatureNotPresent);
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
      sGetLogger()->debug("Extension {} not supported by device {}", required,
                          static_cast<void*>(device));
      return tl::unexpected(VulkanResult::kErrorExtensionNotPresent);
    }
  }

  // At this point we know all required extensions are present.
  IRIS_LOG_LEAVE(sGetLogger());
  return graphicsQueueFamilyIndex;
} // IsPhysicalDeviceGood

/*! \brief Choose the Vulkan physical device - \b MUST only be called from
 * \ref Initialize.
 *
 * \see
 * https://www.khronos.org/registry/vulkan/specs/1.1-extensions/html/vkspec.html#devsandqueues-physical-device-enumeration
 */
static std::error_code ChoosePhysicalDevice() noexcept {
  IRIS_LOG_ENTER(sGetLogger());
  VkResult result;

  // These are the extensions that we require from the device.
  // We are going to enumerate the extensions and check for each one below.
  char const* extensionNames[] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME,
    VK_KHR_MAINTENANCE2_EXTENSION_NAME, "VK_KHX_multiview"};

  // Get the number of physical devices present on the system
  uint32_t numPhysicalDevices;
  result = vkEnumeratePhysicalDevices(sInstance, &numPhysicalDevices, nullptr);
  if (result != VK_SUCCESS) {
    sGetLogger()->error("Cannot enumerate physical devices: {}",
                        to_string(result));
    return make_error_code(result);
  }

  // Get the physical devices present on the system
  absl::FixedArray<VkPhysicalDevice> physicalDevices(numPhysicalDevices);
  result = vkEnumeratePhysicalDevices(sInstance, &numPhysicalDevices,
                                      physicalDevices.data());
  if (result != VK_SUCCESS) {
    sGetLogger()->error("Cannot enumerate physical devices: {}",
                        to_string(result));
    return make_error_code(result);
  }

  // Iterate through each physical device to find one that we can use.
  for (auto&& physicalDevice : physicalDevices) {
    if (auto graphicsQFI =
          IsPhysicalDeviceGood(physicalDevice, extensionNames)) {
      sPhysicalDevice = physicalDevice;
      sGraphicsQueueFamilyIndex = *graphicsQFI;
      break;
    }
  }

  if (sPhysicalDevice == VK_NULL_HANDLE) {
    sGetLogger()->error("No suitable physical device found");
    return Renderer::Error::kNoPhysicalDevice;
  }

  sGetLogger()->debug("Physical Device: {} Graphics QueueFamilyIndex: {}",
                      static_cast<void*>(sPhysicalDevice),
                      sGraphicsQueueFamilyIndex);

  IRIS_LOG_LEAVE(sGetLogger());
  return VulkanResult::kSuccess;
} // ChoosePhysicalDevice

} // namespace iris::Renderer

std::error_code
iris::Renderer::Initialize(gsl::not_null<gsl::czstring<>> appName,
                           std::uint32_t appVersion) noexcept {
  IRIS_LOG_ENTER(sGetLogger());

  ////
  // In order to reduce the verbosity of the Vulakn API, initialization occurs
  // over several sub-functions below. Each function is called in-order and
  // assumes the previous functions have all be called.
  ////

  if (sInitialized) {
    IRIS_LOG_LEAVE(sGetLogger());
    return Error::kAlreadyInitialized;
  }

  ::setenv(
    "VK_LAYER_PATH",
    absl::StrCat(iris::kVulkanSDKDirectory, "/etc/explicit_layer.d").c_str(),
    0);

  flextVkInit();

  if (auto error = InitInstance(appName, appVersion)) {
    IRIS_LOG_LEAVE(sGetLogger());
    return Error::kInitializationFailed;
  }

  flextVkInitInstance(sInstance); // initialize instance function pointers
  CreateDebugReportCallback(); // ignore any returned error

  if (auto error = ChoosePhysicalDevice()) {
    IRIS_LOG_LEAVE(sGetLogger());
    return Error::kInitializationFailed;
  }

  sInitialized = true;
  IRIS_LOG_LEAVE(sGetLogger());
  return Error::kNone;
} // InitializeRenderer

