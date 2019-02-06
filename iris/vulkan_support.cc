#include "absl/container/fixed_array.h"
#include "config.h"
#include "enumerate.h"
#include "error.h"
#include "logging.h"
#include "vulkan_support.h"
#include <algorithm>
#include <cstring>
#include <iterator>
#include <exception>
#include <utility>

namespace iris::Renderer {

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

tl::expected<VkInstance, std::system_error>
CreateInstance(gsl::czstring<> appName, std::uint32_t appVersion,
             gsl::span<gsl::czstring<>> extensionNames,
             gsl::span<gsl::czstring<>> layerNames, bool reportDebug) noexcept {
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
    return tl::unexpected(std::system_error(make_error_code(result),
                           "Cannot enumerate instance extension properties"));
  }

  // Get the instance extension properties.
  absl::FixedArray<VkExtensionProperties> extensionProperties(
    numExtensionProperties);
  if (auto result = vkEnumerateInstanceExtensionProperties(
        nullptr, &numExtensionProperties, extensionProperties.data());
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(make_error_code(result),
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
} // InitInstance

tl::expected<VkDebugUtilsMessengerEXT, std::system_error>
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

tl::expected<std::uint32_t, std::system_error>
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

tl::expected<bool, std::system_error>
IsPhysicalDeviceGood(VkPhysicalDevice physicalDevice,
                     VkPhysicalDeviceFeatures2 features,
                     gsl::span<gsl::czstring<>> extensionNames,
                     VkQueueFlags queueFlags) noexcept {
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
      make_error_code(result), "Cannot enumerate physical device extension properties"));
  }

  // Get the physical device extension properties.
  absl::FixedArray<VkExtensionProperties> extensionProperties(
    numExtensionProperties);
  if (auto result = vkEnumerateDeviceExtensionProperties(
        physicalDevice, nullptr, &numExtensionProperties, extensionProperties.data());
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(
      make_error_code(result), "Cannot enumerate physical device extension properties"));
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

tl::expected<VkPhysicalDevice, std::system_error>
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

tl::expected<VkDevice, std::system_error>
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

tl::expected<VmaAllocator, std::system_error>
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

void DumpPhysicalDevice(VkPhysicalDevice device, std::size_t index,
                        int indentAmount) noexcept {
  IRIS_LOG_ENTER();
  Expects(device != VK_NULL_HANDLE);

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

bool ComparePhysicalDeviceFeatures(VkPhysicalDeviceFeatures2 a,
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

} // namespace iris::Renderer
