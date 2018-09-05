/*! \file
 * \brief \ref iris::Renderer definition.
 */
#include "renderer/renderer.h"
#include "absl/base/macros.h"
#include "absl/container/fixed_array.h"
#include "absl/strings/match.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "dso/dso.h"
#if PLATFORM_COMPILER_MSVC
#pragma warning(push)
#pragma warning(disable : 4127)
#endif
#include "spdlog/spdlog.h"
#if PLATFORM_COMPILER_MSVC
#pragma warning(pop)
#endif
#include "renderer/impl.h"
#include "tl/expected.hpp"
#include <array>
#include <cstdlib>
#include <string>
#include <unordered_map>

namespace iris::Renderer {

static spdlog::logger*
sGetLogger(spdlog::sinks_init_list logSinks = {}) noexcept {
  static std::shared_ptr<spdlog::logger> sLogger;
  if (!sLogger) {
    sLogger = std::make_shared<spdlog::logger>("iris", logSinks);
    sLogger->set_level(spdlog::level::trace);
    spdlog::register_logger(sLogger);
  }

  return sLogger.get();
}

VkInstance sInstance{VK_NULL_HANDLE};
VkDebugReportCallbackEXT sDebugReportCallback{VK_NULL_HANDLE};
VkPhysicalDevice sPhysicalDevice{VK_NULL_HANDLE};
std::uint32_t sGraphicsQueueFamilyIndex{UINT32_MAX};
VkDevice sDevice{VK_NULL_HANDLE};
VkQueue sUnorderedCommandQueue{VK_NULL_HANDLE};

// These are the desired properties of all surfaces for the renderer.
VkSurfaceFormatKHR sSurfaceColorFormat{VK_FORMAT_B8G8R8A8_UNORM,
                                       VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
VkFormat sSurfaceDepthFormat{VK_FORMAT_D32_SFLOAT};
VkSampleCountFlagBits sSurfaceSampleCount{VK_SAMPLE_COUNT_4_BIT};
VkPresentModeKHR sSurfacePresentMode{VK_PRESENT_MODE_FIFO_KHR};

VkRenderPass sRenderPass{VK_NULL_HANDLE};

static bool sInitialized{false};
static bool sRunning{false};
static std::unordered_map<std::string, std::shared_ptr<iris::DSO>> sDSOs;

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
  std::array<char const*, 1> layerNames = {
    {"VK_LAYER_LUNARG_standard_validation"}};
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
    gsl::narrow_cast<std::uint32_t>(layerNames.size());
  ci.ppEnabledLayerNames = layerNames.data();
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

static std::error_code
CreateRenderPass() noexcept {
  IRIS_LOG_ENTER(sGetLogger());
  VkResult result;

  // Our render pass has four attachments:
  // 0: color
  // 1: resolve color
  // 2: depth stencil
  // 3: resolve depth stencil
  //
  // The four are needed to support multi-sampling.
  //
  // The color (0) and depth stencil (2) attachments are the multi-sampled
  // attachments that will match up with framebuffers that are rendered into.
  //
  // The resolve (1, 3) attachments are then used for presenting the final
  // image (1) and sampling for any depth-subpasses (3).
  std::array<VkAttachmentDescription, 4> attachments;

  // The multi-sampled color attachment needs to be cleared on load (loadOp).
  // We don't care what the input layout is (initialLayout) but the final
  // layout must be COLOR_ATTCHMENT_OPTIMAL to allow for resolving.
  attachments[0] = VkAttachmentDescription{
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
  // color and depth. It will be transitioned to PRESENT_SRC_KHR for
  // presentation.
  attachments[1] = VkAttachmentDescription{
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
  // layout must be DEPTH_STENCIL_ATTCHMENT_OPTIMAL to allow for resolving.
  attachments[2] = VkAttachmentDescription{
    0,                                // flags
    sSurfaceDepthFormat,              // format
    sSurfaceSampleCount,              // samples
    VK_ATTACHMENT_LOAD_OP_CLEAR,      // loadOp (color and depth)
    VK_ATTACHMENT_STORE_OP_DONT_CARE, // storeOp (color and depth)
    VK_ATTACHMENT_LOAD_OP_DONT_CARE,  // stencilLoadOp
    VK_ATTACHMENT_STORE_OP_DONT_CARE, // stencilStoreOp
    VK_IMAGE_LAYOUT_UNDEFINED,        // initialLayout
    VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL // finalLayout
  };

  // The resolve depth stencil attachment has a single sample and stores the
  // resolved depth. It will be transitioned to COLOR_ATTACHMENT_OPTIMAL for
  // attachment as an input image to other subpasses.
  attachments[3] = VkAttachmentDescription{
    0,                                       // flags
    sSurfaceDepthFormat,                     // format
    VK_SAMPLE_COUNT_1_BIT,                   // samples
    VK_ATTACHMENT_LOAD_OP_DONT_CARE,         // loadOp (color and depth)
    VK_ATTACHMENT_STORE_OP_STORE,            // storeOp (color and depth)
    VK_ATTACHMENT_LOAD_OP_DONT_CARE,         // stencilLoadOp
    VK_ATTACHMENT_STORE_OP_DONT_CARE,        // stencilStoreOp
    VK_IMAGE_LAYOUT_UNDEFINED,               // initialLayout
    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL // finalLayout
  };

  VkAttachmentReference color{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkAttachmentReference resolve{1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkAttachmentReference depthStencil{
    2, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

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

  std::array<VkSubpassDependency, 2> dependencies;

  dependencies[0] = VkSubpassDependency{
    VK_SUBPASS_EXTERNAL,                           // srcSubpass
    0,                                             // dstSubpass
    VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,          // srcStageMask
    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, // dstStageMask
    VK_ACCESS_MEMORY_READ_BIT,                     // srcAccessMask
    VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, // dstAccessMask
    VK_DEPENDENCY_BY_REGION_BIT             // dependencyFlags
  };

  dependencies[1] = VkSubpassDependency{
    0,                                             // srcSubpass
    VK_SUBPASS_EXTERNAL,                           // dstSubpass
    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, // srcStageMask
    VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,          // dstStageMask
    VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, // srcAccessMask
    VK_ACCESS_MEMORY_READ_BIT,              // dstAccessMask
    VK_DEPENDENCY_BY_REGION_BIT             // dependencyFlags
  };

  VkRenderPassCreateInfo rpci = {};
  rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  rpci.attachmentCount = gsl::narrow_cast<std::uint32_t>(attachments.size());
  rpci.pAttachments = attachments.data();
  rpci.subpassCount = 1;
  rpci.pSubpasses = &subpass;
  rpci.dependencyCount = gsl::narrow_cast<std::uint32_t>(dependencies.size());
  rpci.pDependencies = dependencies.data();

  result = vkCreateRenderPass(sDevice, &rpci, nullptr, &sRenderPass);
  if (result != VK_SUCCESS) {
    sGetLogger()->error("Cannot create device: {}", to_string(result));
    IRIS_LOG_LEAVE(sGetLogger());
    return make_error_code(result);
  }

  sGetLogger()->debug("RenderPass: {}", static_cast<void*>(sRenderPass));
  IRIS_LOG_LEAVE(sGetLogger());
  return VulkanResult::kSuccess;
} // CreateRenderPass

} // namespace iris::Renderer

std::error_code
iris::Renderer::Initialize(gsl::czstring<> appName, std::uint32_t appVersion,
                           spdlog::sinks_init_list logSinks) noexcept {
  IRIS_LOG_ENTER(sGetLogger(logSinks)); // pass logSinks here to initialize logs

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
#if defined(VK_USE_PLATFORM_XLIB_KHR) // we also need the platform-specific surface
    VK_KHR_XLIB_SURFACE_EXTENSION_NAME,
#elif defined(VK_USE_PLATFORM_WIN32_KHR) // we also need the platform-specific surface
    VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
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

  if (auto error = CreateRenderPass()) {
    IRIS_LOG_LEAVE(sGetLogger());
    return Error::kInitializationFailed;
  }

  sInitialized = true;
  sRunning = true;
  IRIS_LOG_LEAVE(sGetLogger());
  return Error::kNone;
} // iris::Renderer::Initialize

void iris::Renderer::Terminate() noexcept {
  sRunning = false;
} // iris::Renderer::Terminate

bool iris::Renderer::IsRunning() noexcept {
  return sRunning;
} // iris::Renderer::IsRunning

void iris::Renderer::Frame() noexcept {
  for (auto [name, dso] : sDSOs) { dso->Frame(); }
} // iris::Renderer::Frame

std::error_code iris::Renderer::Control(std::string_view command) noexcept {
  IRIS_LOG_ENTER(sGetLogger());
  if (command.empty()) {
    sGetLogger()->trace("empty control command");
    IRIS_LOG_LEAVE(sGetLogger());
    return Error::kNone;
  }

  sGetLogger()->debug("Control \"{}\"", command);
  std::vector<std::string_view> components = absl::StrSplit(command, " ");
  auto&& controlCommand = components[0];

  if (absl::StartsWithIgnoreCase(controlCommand, "dso")) {
    std::string dsoName(components[1]);

    // if the DSO hasn't been loaded before, then load and initialie it
    if (sDSOs.find(dsoName) == sDSOs.end()) {
      if (auto dso = DSO::Instantiate(dsoName)) {
        if (auto error = dso->Initialize()) {
          sGetLogger()->error("DSO Initialize Error: {}", error.message());
          IRIS_LOG_LEAVE(sGetLogger());
          return Error::kControlCommandFailed;
        } else {
          sDSOs.emplace(dsoName, std::move(dso));
        }
      }
    }

    // Send control command to the DSO
    if (auto error = sDSOs[dsoName]->Control(command, components)) {
      sGetLogger()->error("DSO Error: {}", error.message());
      IRIS_LOG_LEAVE(sGetLogger());
      return Error::kControlCommandFailed;
    } else {
      IRIS_LOG_LEAVE(sGetLogger());
      return Error::kNone;
    }

  } else {
    sGetLogger()->error("Unknown control command: \"{}\"", command);
    IRIS_LOG_LEAVE(sGetLogger());
    return Error::kUnknownControlCommand;
  }
} // iris::Renderer::Control

std::error_code iris::Renderer::LoadFile(std::string_view fileName) noexcept {
  IRIS_LOG_ENTER(sGetLogger());

  sGetLogger()->debug("Loading {}", fileName);

  IRIS_LOG_LEAVE(sGetLogger());
  return Error::kNone;
} // iris::Renderer::LoadFile
