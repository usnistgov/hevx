/*! \file
 * \brief \ref iris::Renderer definition.
 */
#include "renderer.h"
#include "absl/base/macros.h"
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

static VkInstance sInstance;
static VkDebugReportCallbackEXT sDebugReportCallback;
//static VkPhysicalDevice sPhysicalDevice;
static bool sInitialized{false};

#ifndef NDEBUG
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

static std::error_code InitInstance(char const* appName,
                                    std::uint32_t appVersion) noexcept {
  IRIS_LOG_ENTER(sGetLogger());
  VkResult result;

#ifndef NDEBUG
  char const* layerNames[] = {"VK_LAYER_LUNARG_standard_validation"};
#endif

  char const* extensionNames[] = {"VK_KHR_get_physical_device_properties2",
                                  VK_KHR_SURFACE_EXTENSION_NAME,
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
  ci.enabledLayerCount = ABSL_ARRAYSIZE(layerNames);
  ci.ppEnabledLayerNames = layerNames;
#endif
  ci.enabledExtensionCount = ABSL_ARRAYSIZE(extensionNames);
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

  IRIS_LOG_LEAVE(sGetLogger());
  return VulkanResult::kSuccess;
} // InitInstance

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

  IRIS_LOG_LEAVE(sGetLogger());
#endif

  return VulkanResult::kSuccess;
} // CreateDebugReportCallback

static std::error_code ChoosePhysicalDevice() noexcept {
  IRIS_LOG_ENTER(sGetLogger());
  VkResult result;

  uint32_t numPhysicalDevices;
  result = vkEnumeratePhysicalDevices(sInstance, &numPhysicalDevices, nullptr);
  if (result != VK_SUCCESS) {
    sGetLogger()->error("Cannot enumerate physical devices: {}",
                        to_string(result));
    return make_error_code(result);
  }

  std::vector<VkPhysicalDevice> physicalDevices(numPhysicalDevices);
  result = vkEnumeratePhysicalDevices(sInstance, &numPhysicalDevices,
                                      physicalDevices.data());
  if (result != VK_SUCCESS) {
    sGetLogger()->error("Cannot enumerate physical devices: {}",
                        to_string(result));
    return make_error_code(result);
  }

  for (auto&& physicalDevice : physicalDevices) {
    VkPhysicalDeviceMultiviewProperties multiviewProps = {};
    multiviewProps.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_PROPERTIES;

    VkPhysicalDeviceMaintenance3Properties maint3Props = {};
    maint3Props.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES;
    maint3Props.pNext = &multiviewProps;

    VkPhysicalDeviceProperties2 physicalDeviceProperties = {};
    physicalDeviceProperties.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    physicalDeviceProperties.pNext = &maint3Props;

    vkGetPhysicalDeviceProperties2(physicalDevice, &physicalDeviceProperties);
    auto& deviceProps = physicalDeviceProperties.properties;

    sGetLogger()->debug("Physical Device {} - Type: {} maxMultiviewViews: {}",
                        deviceProps.deviceName,
                        to_string(deviceProps.deviceType),
                        multiviewProps.maxMultiviewViewCount);
  }

  IRIS_LOG_LEAVE(sGetLogger());
  return VulkanResult::kSuccess;
} // ChoosePhysicalDevice

} // namespace iris::Renderer

std::error_code iris::Renderer::Initialize(char const* appName,
                                           std::uint32_t appVersion) noexcept {
  IRIS_LOG_ENTER(sGetLogger());
  if (sInitialized) {
    IRIS_LOG_LEAVE(sGetLogger());
    return Error::kAlreadyInitialized;
  }

  ::setenv(
    "VK_LAYER_PATH",
    absl::StrCat(iris::kVulkanSDKDirectory, "/etc/explicit_layer.d").c_str(),
    0);

  if (auto error = InitInstance(appName, appVersion)) {
    IRIS_LOG_LEAVE(sGetLogger());
    return Error::kInitializationFailed;
  }

  flextVkInit(sInstance);      // initialize instance function pointers
  CreateDebugReportCallback(); // ignore any returned error

  if (auto error = ChoosePhysicalDevice()) {
    IRIS_LOG_LEAVE(sGetLogger());
    return Error::kInitializationFailed;
  }

  sInitialized = true;
  IRIS_LOG_LEAVE(sGetLogger());
  return Error::kNone;
} // InitializeRenderer

