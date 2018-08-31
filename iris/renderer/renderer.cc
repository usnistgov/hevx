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
#if PLATFORM_COMPILER_MSVC
#pragma warning(push)
#pragma warning(disable : 4127)
#endif
#include "spdlog/spdlog.h"
#if PLATFORM_COMPILER_MSVC
#pragma warning(pop)
#endif
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
static std::uint32_t sGraphicsQueueFamilyIndex{UINT32_MAX};
static VkDevice sDevice{VK_NULL_HANDLE};
static VkQueue sUnorderedCommandQueue{VK_NULL_HANDLE};
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
static std::error_code
InitInstance(gsl::czstring<> appName, std::uint32_t appVersion,
             gsl::span<gsl::czstring<>> extensionNames) noexcept {
  IRIS_LOG_ENTER(sGetLogger());
  VkResult result;

  std::uint32_t instanceVersion;
  vkEnumerateInstanceVersion(&instanceVersion); // can only return VK_SUCCESS

  sGetLogger()->debug(
    "Vulkan Instance Version: {}.{}.{}", VK_VERSION_MAJOR(instanceVersion),
    VK_VERSION_MINOR(instanceVersion), VK_VERSION_PATCH(instanceVersion));

  //
  // Enumerate and print out the instance extensions
  //

  // Get the number of instance extension properties.
  std::uint32_t numExtensionProperties;
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
  ci.enabledLayerCount =
    gsl::narrow_cast<std::uint32_t>(ABSL_ARRAYSIZE(layerNames));
  ci.ppEnabledLayerNames = layerNames;
#endif
  ci.enabledExtensionCount =
    gsl::narrow_cast<std::uint32_t>(extensionNames.size());
  ci.ppEnabledExtensionNames = extensionNames.data();

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
  VkPhysicalDeviceFeatures2 physicalDeviceFeatures,
  absl::FixedArray<VkQueueFamilyProperties2> queueFamilyProperties,
  absl::FixedArray<VkExtensionProperties> extensionProperties) {
  auto& deviceProps = physicalDeviceProperties.properties;
  auto& maint3Props =
    *reinterpret_cast<VkPhysicalDeviceMaintenance3Properties*>(
      physicalDeviceProperties.pNext);
  auto& multiviewProps =
    *reinterpret_cast<VkPhysicalDeviceMultiviewProperties*>(maint3Props.pNext);
  auto& features = physicalDeviceFeatures.features;

  sGetLogger()->debug("Physical Device {}", deviceProps.deviceName);
  sGetLogger()->debug("  {} Driver v{}.{}.{} API v{}.{}.{} ",
                      to_string(deviceProps.deviceType),
                      VK_VERSION_MAJOR(deviceProps.driverVersion),
                      VK_VERSION_MINOR(deviceProps.driverVersion),
                      VK_VERSION_PATCH(deviceProps.driverVersion),
                      VK_VERSION_MAJOR(deviceProps.apiVersion),
                      VK_VERSION_MINOR(deviceProps.apiVersion),
                      VK_VERSION_PATCH(deviceProps.apiVersion));

  sGetLogger()->debug("  Features:");
  sGetLogger()->debug("    robustBufferAccess: {}",
                      features.robustBufferAccess == VK_TRUE ? "true"
                                                             : "false");
  sGetLogger()->debug("    fullDrawIndexUint32: {}",
                      features.fullDrawIndexUint32 == VK_TRUE ? "true"
                                                              : "false");
  sGetLogger()->debug("    imageCubeArray: {}",
                      features.imageCubeArray == VK_TRUE ? "true" : "false");
  sGetLogger()->debug("    independentBlend: {}",
                      features.independentBlend == VK_TRUE ? "true" : "false");
  sGetLogger()->debug("    geometryShader: {}",
                      features.geometryShader == VK_TRUE ? "true" : "false");
  sGetLogger()->debug("    tessellationShader: {}",
                      features.tessellationShader == VK_TRUE ? "true"
                                                             : "false");
  sGetLogger()->debug("    sampleRateShading: {}",
                      features.sampleRateShading == VK_TRUE ? "true" : "false");
  sGetLogger()->debug("    dualSrcBlend: {}",
                      features.dualSrcBlend == VK_TRUE ? "true" : "false");
  sGetLogger()->debug("    logicOp: {}",
                      features.logicOp == VK_TRUE ? "true" : "false");
  sGetLogger()->debug("    multiDrawIndirect: {}",
                      features.multiDrawIndirect == VK_TRUE ? "true" : "false");
  sGetLogger()->debug("    drawIndirectFirstInstance: {}",
                      features.drawIndirectFirstInstance == VK_TRUE ? "true"
                                                                    : "false");
  sGetLogger()->debug("    depthClamp: {}",
                      features.depthClamp == VK_TRUE ? "true" : "false");
  sGetLogger()->debug("    depthBiasClamp: {}",
                      features.depthBiasClamp == VK_TRUE ? "true" : "false");
  sGetLogger()->debug("    fillModeNonSolid: {}",
                      features.fillModeNonSolid == VK_TRUE ? "true" : "false");
  sGetLogger()->debug("    depthBounds: {}",
                      features.depthBounds == VK_TRUE ? "true" : "false");
  sGetLogger()->debug("    wideLines: {}",
                      features.wideLines == VK_TRUE ? "true" : "false");
  sGetLogger()->debug("    largePoints: {}",
                      features.largePoints == VK_TRUE ? "true" : "false");
  sGetLogger()->debug("    alphaToOne: {}",
                      features.alphaToOne == VK_TRUE ? "true" : "false");
  sGetLogger()->debug("    multiViewport: {}",
                      features.multiViewport == VK_TRUE ? "true" : "false");
  sGetLogger()->debug("    samplerAnisotropy: {}",
                      features.samplerAnisotropy == VK_TRUE ? "true" : "false");
  sGetLogger()->debug("    textureCompressionETC2: {}",
                      features.textureCompressionETC2 == VK_TRUE ? "true"
                                                                 : "false");
  sGetLogger()->debug("    textureCompressionASTC_LDR: {}",
                      features.textureCompressionASTC_LDR == VK_TRUE ? "true"
                                                                     : "false");
  sGetLogger()->debug("    textureCompressionBC: {}",
                      features.textureCompressionBC == VK_TRUE ? "true"
                                                               : "false");
  sGetLogger()->debug("    occlusionQueryPrecise: {}",
                      features.occlusionQueryPrecise == VK_TRUE ? "true"
                                                                : "false");
  sGetLogger()->debug("    pipelineStatisticsQuery: {}",
                      features.pipelineStatisticsQuery == VK_TRUE ? "true"
                                                                  : "false");
  sGetLogger()->debug(
    "    vertexPipelineStoresAndAtomics: {}",
    features.vertexPipelineStoresAndAtomics == VK_TRUE ? "true" : "false");
  sGetLogger()->debug("    fragmentStoresAndAtomics: {}",
                      features.fragmentStoresAndAtomics == VK_TRUE ? "true"
                                                                   : "false");
  sGetLogger()->debug("    shaderTessellationAndGeometryPointSize: {}",
                      features.shaderTessellationAndGeometryPointSize == VK_TRUE
                        ? "true"
                        : "false");
  sGetLogger()->debug("    shaderImageGatherExtended: {}",
                      features.shaderImageGatherExtended == VK_TRUE ? "true"
                                                                    : "false");
  sGetLogger()->debug(
    "    shaderStorageImageExtendedFormats: {}",
    features.shaderStorageImageExtendedFormats == VK_TRUE ? "true" : "false");
  sGetLogger()->debug(
    "    shaderStorageImageMultisample: {}",
    features.shaderStorageImageMultisample == VK_TRUE ? "true" : "false");
  sGetLogger()->debug(
    "    shaderStorageImageReadWithoutFormat: {}",
    features.shaderStorageImageReadWithoutFormat == VK_TRUE ? "true" : "false");
  sGetLogger()->debug("    shaderStorageImageWriteWithoutFormat: {}",
                      features.shaderStorageImageWriteWithoutFormat == VK_TRUE
                        ? "true"
                        : "false");
  sGetLogger()->debug(
    "    shaderUniformBufferArrayDynamicIndexing: {}",
    features.shaderUniformBufferArrayDynamicIndexing == VK_TRUE ? "true"
                                                                : "false");
  sGetLogger()->debug("    shaderSampledImageArrayDynamicIndexing: {}",
                      features.shaderSampledImageArrayDynamicIndexing == VK_TRUE
                        ? "true"
                        : "false");
  sGetLogger()->debug(
    "    shaderStorageBufferArrayDynamicIndexing: {}",
    features.shaderStorageBufferArrayDynamicIndexing == VK_TRUE ? "true"
                                                                : "false");
  sGetLogger()->debug("    shaderStorageImageArrayDynamicIndexing: {}",
                      features.shaderStorageImageArrayDynamicIndexing == VK_TRUE
                        ? "true"
                        : "false");
  sGetLogger()->debug("    shaderClipDistance: {}",
                      features.shaderClipDistance == VK_TRUE ? "true"
                                                             : "false");
  sGetLogger()->debug("    shaderCullDistance: {}",
                      features.shaderCullDistance == VK_TRUE ? "true"
                                                             : "false");
  sGetLogger()->debug("    shaderFloat64: {}",
                      features.shaderFloat64 == VK_TRUE ? "true" : "false");
  sGetLogger()->debug("    shaderInt64: {}",
                      features.shaderInt64 == VK_TRUE ? "true" : "false");
  sGetLogger()->debug("    shaderInt16: {}",
                      features.shaderInt16 == VK_TRUE ? "true" : "false");
  sGetLogger()->debug("    shaderResourceResidency: {}",
                      features.shaderResourceResidency == VK_TRUE ? "true"
                                                                  : "false");
  sGetLogger()->debug("    shaderResourceMinLod: {}",
                      features.shaderResourceMinLod == VK_TRUE ? "true"
                                                               : "false");
  sGetLogger()->debug("    sparseBinding: {}",
                      features.sparseBinding == VK_TRUE ? "true" : "false");
  sGetLogger()->debug("    sparseResidencyBuffer: {}",
                      features.sparseResidencyBuffer == VK_TRUE ? "true"
                                                                : "false");
  sGetLogger()->debug("    sparseResidencyImage2D: {}",
                      features.sparseResidencyImage2D == VK_TRUE ? "true"
                                                                 : "false");
  sGetLogger()->debug("    sparseResidencyImage3D: {}",
                      features.sparseResidencyImage3D == VK_TRUE ? "true"
                                                                 : "false");
  sGetLogger()->debug("    sparseResidency2Samples: {}",
                      features.sparseResidency2Samples == VK_TRUE ? "true"
                                                                  : "false");
  sGetLogger()->debug("    sparseResidency4Samples: {}",
                      features.sparseResidency4Samples == VK_TRUE ? "true"
                                                                  : "false");
  sGetLogger()->debug("    sparseResidency8Samples: {}",
                      features.sparseResidency8Samples == VK_TRUE ? "true"
                                                                  : "false");
  sGetLogger()->debug("    sparseResidency16Samples: {}",
                      features.sparseResidency16Samples == VK_TRUE ? "true"
                                                                   : "false");
  sGetLogger()->debug("    sparseResidencyAliased: {}",
                      features.sparseResidencyAliased == VK_TRUE ? "true"
                                                                 : "false");
  sGetLogger()->debug("    variableMultisampleRate: {}",
                      features.variableMultisampleRate == VK_TRUE ? "true"
                                                                  : "false");
  sGetLogger()->debug("    inheritedQueries: {}",
                      features.inheritedQueries == VK_TRUE ? "true" : "false");

  sGetLogger()->debug("  Limits:");
  sGetLogger()->debug("    maxMultiviewViews: {}",
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

/*! \brief Compare two VkPhysicalDeviceFeatures2 structures.
 *
 * \see
 * https://www.khronos.org/registry/vulkan/specs/1.1-extensions/html/vkspec.html#features-features
 */
static bool
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
static tl::expected<std::uint32_t, std::error_code>
IsPhysicalDeviceGood(VkPhysicalDevice device,
                     VkPhysicalDeviceFeatures2 features,
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

  DumpPhysicalDevice(physicalDeviceProperties, physicalDeviceFeatures,
                     queueFamilyProperties, extensionProperties);

  //
  // Check all queried data to see if this device is good.
  //

  // Check for any required features
  if (!ComparePhysicalDeviceFeatures(physicalDeviceFeatures, features)) {
    sGetLogger()->debug("Requested feature not supported by device {}",
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
static std::error_code
ChoosePhysicalDevice(VkPhysicalDeviceFeatures2 features,
                     gsl::span<gsl::czstring<>> extensionNames) noexcept {
  IRIS_LOG_ENTER(sGetLogger());
  VkResult result;

  // Get the number of physical devices present on the system
  std::uint32_t numPhysicalDevices;
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
          IsPhysicalDeviceGood(physicalDevice, features, extensionNames)) {
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

/*! \brief Create the Vulkan logical device - \b MUST only be called from
 * \ref Initialize.
 *
 * \see
 * https://www.khronos.org/registry/vulkan/specs/1.1-extensions/html/vkspec.html#devsandqueues-devices
 * \see
 * https://www.khronos.org/registry/vulkan/specs/1.1-extensions/html/vkspec.html#devsandqueues-queues
 */
static std::error_code
CreateDeviceAndQueues(VkPhysicalDeviceFeatures2 physicalDeviceFeatures,
                      gsl::span<gsl::czstring<>> extensionNames) noexcept {
  IRIS_LOG_ENTER(sGetLogger());
  VkResult result;

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

  float const priority = 1.f;
  VkDeviceQueueCreateInfo qci = {};
  qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  qci.queueFamilyIndex = sGraphicsQueueFamilyIndex;
  qci.queueCount = queueFamilyProperties[sGraphicsQueueFamilyIndex]
                     .queueFamilyProperties.queueCount;
  qci.pQueuePriorities = &priority;

  VkDeviceCreateInfo ci = {};
  ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  ci.pNext = &physicalDeviceFeatures;
  ci.queueCreateInfoCount = 1;
  ci.pQueueCreateInfos = &qci;
  ci.enabledExtensionCount =
    gsl::narrow_cast<std::uint32_t>(extensionNames.size());
  ci.ppEnabledExtensionNames = extensionNames.data();

  result = vkCreateDevice(sPhysicalDevice, &ci, nullptr, &sDevice);
  if (result != VK_SUCCESS) {
    sGetLogger()->error("Cannot create device: {}", to_string(result));
    IRIS_LOG_LEAVE(sGetLogger());
    return make_error_code(result);
  }

  vkGetDeviceQueue(sDevice, sGraphicsQueueFamilyIndex, 0,
                   &sUnorderedCommandQueue);

  sGetLogger()->debug("Device: {}", static_cast<void*>(sDevice));
  sGetLogger()->debug("Unordered Command Queue: {}",
                      static_cast<void*>(sUnorderedCommandQueue));
  IRIS_LOG_LEAVE(sGetLogger());
  return VulkanResult::kSuccess;
} // CreateDevice

} // namespace iris::Renderer

std::error_code iris::Renderer::Initialize(gsl::czstring<> appName,
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

  // These are the extensions that we require from the instance.
  char const* instanceExtensionNames[] = {
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

  // These are the features that we require from the physical device.
  VkPhysicalDeviceFeatures2 physicalDeviceFeatures = {};
  physicalDeviceFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  physicalDeviceFeatures.features.fullDrawIndexUint32 = VK_TRUE;
  physicalDeviceFeatures.features.fillModeNonSolid = VK_TRUE;
  physicalDeviceFeatures.features.multiViewport = VK_TRUE;
  physicalDeviceFeatures.features.pipelineStatisticsQuery = VK_TRUE;

  // These are the extensions that we require from the physical device.
  char const* physicalDeviceExtensionNames[] = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME,
    VK_KHR_MAINTENANCE2_EXTENSION_NAME,
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

  if (auto error = InitInstance(appName, appVersion, instanceExtensionNames)) {
    IRIS_LOG_LEAVE(sGetLogger());
    return Error::kInitializationFailed;
  }

  flextVkInitInstance(sInstance); // initialize instance function pointers
  CreateDebugReportCallback(); // ignore any returned error

  if (auto error = ChoosePhysicalDevice(physicalDeviceFeatures,
                                        physicalDeviceExtensionNames)) {
    IRIS_LOG_LEAVE(sGetLogger());
    return Error::kInitializationFailed;
  }

  if (auto error = CreateDeviceAndQueues(physicalDeviceFeatures,
                                         physicalDeviceExtensionNames)) {
    IRIS_LOG_LEAVE(sGetLogger());
    return Error::kInitializationFailed;
  }

  sInitialized = true;
  IRIS_LOG_LEAVE(sGetLogger());
  return Error::kNone;
} // InitializeRenderer

