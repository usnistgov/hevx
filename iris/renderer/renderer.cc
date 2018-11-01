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
#include "error.h"
#include "imgui.h"
#include "protos.h"
#include "renderer/impl.h"
#include "renderer/io.h"
#include "renderer/tasks.h"
#include "renderer/window.h"
#if PLATFORM_COMPILER_MSVC
#pragma warning(push)
#elif PLATFORM_COMPILER_GCC
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#endif
#include "shaderc/shaderc.hpp"
#if PLATFORM_COMPILER_MSVC
#pragma warning(pop)
#elif PLATFORM_COMPILER_GCC
#pragma GCC diagnostic pop
#endif
#if PLATFORM_COMPILER_MSVC
#pragma warning(push)
#pragma warning(disable : 4127)
#endif
#include "spdlog/spdlog.h"
#if PLATFORM_COMPILER_MSVC
#pragma warning(pop)
#endif
#include "tl/expected.hpp"
#include "wsi/error.h"
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
  ::iris::GetLogger()->trace("ENTER: {} ({}:{})", __func__, __FILE__, __LINE__)
//! \brief Logs leave from a function.
#define IRIS_LOG_LEAVE()                                                       \
  ::iris::GetLogger()->trace("LEAVE: {} ({}:{})", __func__, __FILE__, __LINE__)

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
VkCommandPool sGraphicsCommandPool{VK_NULL_HANDLE};
VkFence sOneTimeSubmit{VK_NULL_HANDLE};
VkFence sFrameComplete{VK_NULL_HANDLE};
VmaAllocator sAllocator;
tbb::concurrent_queue<TaskResult> sTasksResultsQueue;

// These are the desired properties of all surfaces for the renderer.
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
absl::FixedArray<VkCommandBuffer> sCommandBuffers{2};
std::uint32_t sCommandBufferIndex{0};

/////
//
// Additional static private variables
//
/////

static bool sInitialized{false};
static bool sRunning{false};

VkSemaphore sImagesReadyForPresent{VK_NULL_HANDLE};

std::string const sBlankFSQVertexShaderSource = R"(
#version 450
layout(location = 0) out vec2 fragCoord;
void main() {
  fragCoord = vec2((gl_VertexIndex << 1) & 2, (gl_VertexIndex & 2));
    gl_Position = vec4(fragCoord * 2.0 - 1.0, 0.f, 1.0);
})";

std::string const sBlankFSQFragmentShaderSource = R"(
#version 450
layout(location = 0) in vec2 fragCoord;
layout(location = 0) out vec4 fragColor;
void main() {
})";

std::string const sUIVertexShaderSource = R"(
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

std::string const sUIFragmentShaderSource = R"(
#version 450 core
layout(set = 0, binding = 0) uniform sampler2D sTexture;
layout(location = 0) in vec4 Color;
layout(location = 1) in vec2 UV;
layout(location = 0) out vec4 fColor;
void main() {
  fColor = Color * texture(sTexture, UV.st);
})";

struct Pipeline {
  VkPipelineLayout layout{VK_NULL_HANDLE};
  VkPipeline pipeline{VK_NULL_HANDLE};

  Pipeline() noexcept = default;
  Pipeline(Pipeline const&) = delete;
  Pipeline(Pipeline&&) noexcept;
  Pipeline& operator=(Pipeline const&) = delete;
  Pipeline& operator=(Pipeline&&) noexcept;
  ~Pipeline() noexcept;
}; // struct PipelineAndLayout

Pipeline::Pipeline(Pipeline&& other) noexcept
  : layout(other.layout)
  , pipeline(other.pipeline) {
  other.layout = VK_NULL_HANDLE;
  other.pipeline = VK_NULL_HANDLE;
} // Pipeline::Pipeline

Pipeline& Pipeline::operator=(Pipeline&& rhs) noexcept {
  if (this == &rhs) return *this;
  layout = rhs.layout;
  pipeline = rhs.pipeline;
  rhs.layout = VK_NULL_HANDLE;
  rhs.pipeline = VK_NULL_HANDLE;
  return *this;
} // Pipeline::operator=

Pipeline::~Pipeline() noexcept {
  if (pipeline != VK_NULL_HANDLE) {
    vkDestroyPipeline(sDevice, pipeline, nullptr);
  }
  if (layout != VK_NULL_HANDLE) {
    vkDestroyPipelineLayout(sDevice, layout, nullptr);
  }
} // Pipeline::~Pipeline

static absl::flat_hash_map<std::string, Pipeline>& Pipelines() noexcept {
  static absl::flat_hash_map<std::string, Pipeline> sPipelines;
  return sPipelines;
} // Pipelines

static absl::flat_hash_map<std::string, iris::Renderer::Window>&
Windows() noexcept {
  static absl::flat_hash_map<std::string, iris::Renderer::Window> sWindows;
  return sWindows;
} // Windows

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
static std::error_code InitInstance(gsl::czstring<> appName,
                                    std::uint32_t appVersion,
                                    gsl::span<gsl::czstring<>> extensionNames,
                                    gsl::span<gsl::czstring<>> layerNames,
                                    bool reportDebug) noexcept {
  IRIS_LOG_ENTER();
  VkResult result;

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
  result = vkEnumerateInstanceExtensionProperties(
    nullptr, &numExtensionProperties, nullptr);
  if (result != VK_SUCCESS) {
    GetLogger()->error("Cannot enumerate instance extension properties: {}",
                       to_string(result));
    return make_error_code(result);
  }

  // Get the instance extension properties.
  absl::FixedArray<VkExtensionProperties> extensionProperties(
    numExtensionProperties);
  result = vkEnumerateInstanceExtensionProperties(
    nullptr, &numExtensionProperties, extensionProperties.data());
  if (result != VK_SUCCESS) {
    GetLogger()->error("Cannot enumerate instance extension properties: {}",
                       to_string(result));
    return make_error_code(result);
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

  result = vkCreateInstance(&ci, nullptr, &sInstance);
  if (result != VK_SUCCESS) {
    GetLogger()->error("Cannot create instance: {}", to_string(result));
    IRIS_LOG_LEAVE();
    return make_error_code(result);
  }

  flextVkInitInstance(sInstance); // initialize instance function pointers

  IRIS_LOG_LEAVE();
  return VulkanResult::kSuccess;
} // InitInstance

static std::error_code CreateDebugUtilsMessenger() noexcept {
  IRIS_LOG_ENTER();
  VkResult result;

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

  result = vkCreateDebugUtilsMessengerEXT(sInstance, &dumci, nullptr,
                                          &sDebugUtilsMessenger);
  if (result != VK_SUCCESS) {
    GetLogger()->warn("Cannot create debug utils messenger: {}",
                      to_string(result));
  }

  IRIS_LOG_LEAVE();
  return VulkanResult::kSuccess;
} // CreateDebugUtilsMessenger

static void DumpPhysicalDevice(VkPhysicalDevice device, std::size_t index,
                               int indentAmount = 0) noexcept {
  IRIS_LOG_ENTER();
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
    GetLogger()->warn("Cannot enumerate device extension properties: {}",
                      to_string(result));
  }

  // Get the physical device extension properties.
  absl::FixedArray<VkExtensionProperties> extensionProperties(
    numExtensionProperties);
  result = vkEnumerateDeviceExtensionProperties(
    device, nullptr, &numExtensionProperties, extensionProperties.data());
  if (result != VK_SUCCESS) {
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
  IRIS_LOG_ENTER();
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
    GetLogger()->warn("Cannot enumerate device extension properties: {}",
                      to_string(result));
    return tl::unexpected(make_error_code(result));
  }

  // Get the physical device extension properties.
  absl::FixedArray<VkExtensionProperties> extensionProperties(
    numExtensionProperties);
  result = vkEnumerateDeviceExtensionProperties(
    device, nullptr, &numExtensionProperties, extensionProperties.data());
  if (result != VK_SUCCESS) {
    GetLogger()->warn("Cannot enumerate device extension properties: {}",
                      to_string(result));
    return tl::unexpected(make_error_code(result));
  }

  //
  // Check all queried data to see if this device is good.
  //

  // Check for any required features
  if (!ComparePhysicalDeviceFeatures(physicalDeviceFeatures, features)) {
    GetLogger()->debug(
      "Requested feature not supported by device"); // {}",
                                                    // static_cast<void*>(device));
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
    GetLogger()->debug(
      "No graphics queue supported by device"); // {}",
                                                // static_cast<void*>(device));
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
      GetLogger()->debug("Extension {} not supported by device",
                         required); // {}", required,
                                    // static_cast<void*>(device));
      return tl::unexpected(VulkanResult::kErrorExtensionNotPresent);
    }
  }

  // At this point we know all required extensions are present.
  IRIS_LOG_LEAVE();
  return graphicsQueueFamilyIndex;
} // IsPhysicalDeviceGood

static void FindDeviceGroup() {
  IRIS_LOG_ENTER();
  VkResult result;

  std::uint32_t numPhysicalDeviceGroups;
  result = vkEnumeratePhysicalDeviceGroups(sInstance, &numPhysicalDeviceGroups,
                                           nullptr);
  if (result != VK_SUCCESS) {
    GetLogger()->error("Cannot enumerate physical device groups: {}",
                       to_string(result));
    return;
  }

  absl::FixedArray<VkPhysicalDeviceGroupProperties>
    physicalDeviceGroupProperties(numPhysicalDeviceGroups);
  for (std::uint32_t i = 0; i < numPhysicalDeviceGroups; ++i) {
    physicalDeviceGroupProperties[i].sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_GROUP_PROPERTIES;
    physicalDeviceGroupProperties[i].pNext = nullptr;
  }

  result = vkEnumeratePhysicalDeviceGroups(
    sInstance, &numPhysicalDeviceGroups, physicalDeviceGroupProperties.data());
  if (result != VK_SUCCESS) {
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
static std::error_code
ChoosePhysicalDevice(VkPhysicalDeviceFeatures2 features,
                     gsl::span<gsl::czstring<>> extensionNames) noexcept {
  IRIS_LOG_ENTER();
  VkResult result;

  // Get the number of physical devices present on the system
  std::uint32_t numPhysicalDevices;
  result = vkEnumeratePhysicalDevices(sInstance, &numPhysicalDevices, nullptr);
  if (result != VK_SUCCESS) {
    GetLogger()->error("Cannot enumerate physical devices: {}",
                       to_string(result));
    return make_error_code(result);
  }

  // Get the physical devices present on the system
  absl::FixedArray<VkPhysicalDevice> physicalDevices(numPhysicalDevices);
  result = vkEnumeratePhysicalDevices(sInstance, &numPhysicalDevices,
                                      physicalDevices.data());
  if (result != VK_SUCCESS) {
    GetLogger()->error("Cannot enumerate physical devices: {}",
                       to_string(result));
    return make_error_code(result);
  }

  // Iterate and dump every physical device
  GetLogger()->debug("{} physical devices", numPhysicalDevices);
  for (std::uint32_t i = 0; i < numPhysicalDevices; ++i) {
    DumpPhysicalDevice(physicalDevices[i], i);
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
    GetLogger()->error("No suitable physical device found");
    return Error::kNoPhysicalDevice;
  }

  IRIS_LOG_LEAVE();
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
  IRIS_LOG_ENTER();
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

  result = vkCreateDevice(sPhysicalDevice, &ci, nullptr, &sDevice);
  if (result != VK_SUCCESS) {
    GetLogger()->error("Cannot create device: {}", to_string(result));
    IRIS_LOG_LEAVE();
    return make_error_code(result);
  }

  vkGetDeviceQueue(sDevice, sGraphicsQueueFamilyIndex, 0,
                   &sGraphicsCommandQueue);

  IRIS_LOG_LEAVE();
  return VulkanResult::kSuccess;
} // CreateDevice

static std::error_code CreateCommandPool() noexcept {
  IRIS_LOG_ENTER();
  VkResult result;

  VkCommandPoolCreateInfo ci = {};
  ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  ci.queueFamilyIndex = sGraphicsQueueFamilyIndex;

  result = vkCreateCommandPool(sDevice, &ci, nullptr, &sGraphicsCommandPool);
  if (result != VK_SUCCESS) {
    GetLogger()->error("Cannot create command pool: {}", to_string(result));
    IRIS_LOG_LEAVE();
    return make_error_code(result);
  }

  NameObject(VK_OBJECT_TYPE_COMMAND_POOL, sGraphicsCommandPool,
             "sGraphicsCommandPool");

  IRIS_LOG_LEAVE();
  return VulkanResult::kSuccess;
} // CreateCommandPool

static std::error_code CreateFencesAndSemaphores() noexcept {
  IRIS_LOG_ENTER();
  VkResult result;

  VkFenceCreateInfo fci = {};
  fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

  result = vkCreateFence(sDevice, &fci, nullptr, &sOneTimeSubmit);
  if (result != VK_SUCCESS) {
    GetLogger()->error("Cannot create fence: {}", to_string(result));
    IRIS_LOG_LEAVE();
    return make_error_code(result);
  }

  fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;

  result = vkCreateFence(sDevice, &fci, nullptr, &sFrameComplete);
  if (result != VK_SUCCESS) {
    GetLogger()->error("Cannot create fence: {}", to_string(result));
    IRIS_LOG_LEAVE();
    return make_error_code(result);
  }

  VkSemaphoreCreateInfo sci = {};
  sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

  result = vkCreateSemaphore(sDevice, &sci, nullptr, &sImagesReadyForPresent);
  if (result != VK_SUCCESS) {
    GetLogger()->error("Cannot create semaphore: {}", to_string(result));
    IRIS_LOG_LEAVE();
    return make_error_code(result);
  }

  NameObject(VK_OBJECT_TYPE_FENCE, sOneTimeSubmit, "sOneTimeSubmit");
  NameObject(VK_OBJECT_TYPE_FENCE, sFrameComplete, "sFrameComplete");
  NameObject(VK_OBJECT_TYPE_SEMAPHORE, sImagesReadyForPresent,
             "sImagesReadyForPresent");

  IRIS_LOG_LEAVE();
  return VulkanResult::kSuccess;
} // CreateFencesAndSemaphores

static std::error_code CreateAllocator() noexcept {
  IRIS_LOG_ENTER();
  VkResult result;

  VmaAllocatorCreateInfo allocatorInfo = {};
  allocatorInfo.flags = VMA_ALLOCATOR_CREATE_KHR_DEDICATED_ALLOCATION_BIT;
  allocatorInfo.physicalDevice = sPhysicalDevice;
  allocatorInfo.device = sDevice;

  result = vmaCreateAllocator(&allocatorInfo, &sAllocator);
  if (result != VK_SUCCESS) {
    GetLogger()->error("Cannot create allocator: {}", to_string(result));
    IRIS_LOG_LEAVE();
    return make_error_code(result);
  }

  IRIS_LOG_LEAVE();
  return VulkanResult::kSuccess;
} // CreateAllocator

static std::error_code CreateRenderPass() noexcept {
  IRIS_LOG_ENTER();
  VkResult result;

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

  std::vector<VkSubpassDependency> dependencies{
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

  result = vkCreateRenderPass(sDevice, &rpci, nullptr, &sRenderPass);
  if (result != VK_SUCCESS) {
    GetLogger()->error("Cannot create render pass: {}", to_string(result));
    IRIS_LOG_LEAVE();
    return make_error_code(result);
  }

  NameObject(VK_OBJECT_TYPE_RENDER_PASS, sRenderPass, "sRenderPass");

  IRIS_LOG_LEAVE();
  return VulkanResult::kSuccess;
} // CreateRenderPass

static std::error_code AllocateCommandBuffers() noexcept {
  IRIS_LOG_ENTER();
  VkResult result;

  VkCommandBufferAllocateInfo ai = {};
  ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  ai.commandPool = sGraphicsCommandPool;
  ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  ai.commandBufferCount =
    gsl::narrow_cast<std::uint32_t>(sCommandBuffers.size());

  result = vkAllocateCommandBuffers(sDevice, &ai, sCommandBuffers.data());
  if (result != VK_SUCCESS) {
    GetLogger()->error("Error allocating command buffers: {}",
                       to_string(result));
    IRIS_LOG_LEAVE();
    return make_error_code(result);
  }

  for (auto&& commandBuffer : sCommandBuffers) {
    NameObject(VK_OBJECT_TYPE_COMMAND_BUFFER, commandBuffer, "sCommandBuffers");
  }

  IRIS_LOG_LEAVE();
  return VulkanResult::kSuccess;
} // AllocateCommandBuffers

class ShaderIncluder : public shaderc::CompileOptions::IncluderInterface {
public:
  shaderc_include_result*
  GetInclude(char const* requested_source, shaderc_include_type type,
             char const* requesting_source,
             size_t include_depth[[maybe_unused]]) override;

  void ReleaseInclude(shaderc_include_result* data) override;

private:
  struct Include {
    filesystem::path path;
    std::string source;
    std::unique_ptr<shaderc_include_result> result;

    Include(filesystem::path p, std::string s)
      : path(std::move(p))
      , source(std::move(s))
      , result(new shaderc_include_result) {}
  }; // struct Include

  std::vector<Include> includes_{};
}; // class ShaderIncluder

shaderc_include_result* ShaderIncluder::GetInclude(
  char const* requested_source, shaderc_include_type type,
  char const* requesting_source, size_t include_depth[[maybe_unused]]) {
  IRIS_LOG_ENTER();
  filesystem::path path(requested_source);

  if (type == shaderc_include_type_relative) {
    filesystem::path parent(requesting_source);
    parent = parent.parent_path();
    path = parent / path;
  }

  try {
    if (!filesystem::exists(path)) { path.clear(); }
  } catch (...) { path.clear(); }

  if (!path.empty()) {
    if (auto s = io::ReadFile(path)) {
      includes_.push_back(Include(path, std::string(s->data(), s->size())));
    } else {
      includes_.push_back(Include(path, s.error().message()));
    }
  } else {
    includes_.push_back(Include(path, "file not found"));
  }

  Include& include = includes_.back();
  shaderc_include_result* result = include.result.get();

  result->source_name_length = include.path.string().size();
  result->source_name = include.path.string().c_str();
  result->content_length = include.source.size();
  result->content = include.source.data();
  result->user_data = nullptr;

  IRIS_LOG_LEAVE();
  return result;
} // ShaderIncluder::GetInclude

void ShaderIncluder::ReleaseInclude(shaderc_include_result* result) {
  IRIS_LOG_ENTER();
  for (std::size_t i = 0; i < includes_.size(); ++i) {
    if (includes_[i].result.get() == result) {
      includes_.erase(includes_.begin() + i);
      break;
    }
  }
  IRIS_LOG_LEAVE();
} // ShaderIncluder::ReleaseInclude

tl::expected<std::vector<std::uint32_t>, std::string>
CompileShader(std::string_view source, VkShaderStageFlagBits shaderStage,
              filesystem::path const& path, std::string const& entryPoint) {
  IRIS_LOG_ENTER();
  shaderc::Compiler compiler;
  shaderc::CompileOptions options;
  options.SetOptimizationLevel(shaderc_optimization_level_performance);
  options.SetIncluder(std::make_unique<ShaderIncluder>());

  auto const kind = [&shaderStage]() {
    if ((shaderStage & VK_SHADER_STAGE_VERTEX_BIT)) {
      return shaderc_vertex_shader;
    } else if ((shaderStage & VK_SHADER_STAGE_FRAGMENT_BIT)) {
      return shaderc_fragment_shader;
    } else {
      GetLogger()->critical("Unhandled shaderStage: {}", shaderStage);
      std::terminate();
    }
  }();

  auto spv = compiler.CompileGlslToSpv(source.data(), source.size(), kind,
                                       path.string().c_str(),
                                       entryPoint.c_str(), options);
  if (spv.GetCompilationStatus() != shaderc_compilation_status_success) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(spv.GetErrorMessage());
  }

  std::vector<std::uint32_t> code;
  std::copy(std::begin(spv), std::end(spv), std::back_inserter(code));

  IRIS_LOG_LEAVE();
  return code;
} // CompileShader

tl::expected<VkShaderModule, std::error_code>
CreateShaderFromSource(std::string_view source,
                       VkShaderStageFlagBits shaderStage,
                       std::string const& entry = "main") noexcept {
  IRIS_LOG_ENTER();
  VkResult result;

  if (auto code = CompileShader(source, shaderStage, "", entry)) {
    VkShaderModuleCreateInfo smci = {};
    smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    // codeSize is bytes, not count of words
    smci.codeSize = gsl::narrow_cast<std::uint32_t>(code->size() * 4);
    smci.pCode = code->data();

    VkShaderModule module;
    result = vkCreateShaderModule(sDevice, &smci, nullptr, &module);
    if (result != VK_SUCCESS) {
      GetLogger()->error("Cannot create shader module: {}", to_string(result));
      IRIS_LOG_LEAVE();
      return tl::unexpected(make_error_code(result));
    }

    IRIS_LOG_LEAVE();
    return module;
  } else {
    GetLogger()->error("Cannot compile shader: {}", code.error());
    IRIS_LOG_LEAVE();
    return tl::unexpected(Error::kShaderCompileFailed);
  }
} // CreateShaderFromSource

tl::expected<VkShaderModule, std::error_code>
CreateShaderFromFile(filesystem::path const& path,
                     VkShaderStageFlagBits shaderStage,
                     std::string const& entry = "main") noexcept {
  IRIS_LOG_ENTER();
  VkResult result;

  std::vector<char> source;
  if (auto s = io::ReadFile(path)) {
    source = std::move(*s);
  } else {
    return tl::unexpected(s.error());
  }

  if (auto code = CompileShader({source.data(), source.size()}, shaderStage,
                                path, entry)) {
    VkShaderModuleCreateInfo smci = {};
    smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    // codeSize is bytes, not count of words
    smci.codeSize = gsl::narrow_cast<std::uint32_t>(code->size() * 4);
    smci.pCode = code->data();

    VkShaderModule module;
    result = vkCreateShaderModule(sDevice, &smci, nullptr, &module);
    if (result != VK_SUCCESS) {
      GetLogger()->error("Cannot create shader module: {}", to_string(result));
      IRIS_LOG_LEAVE();
      return tl::unexpected(make_error_code(result));
    }

    IRIS_LOG_LEAVE();
    return module;
  } else {
    GetLogger()->error("Cannot compile shader: {}", code.error());
    IRIS_LOG_LEAVE();
    return tl::unexpected(Error::kShaderCompileFailed);
  }
} // CreateShaderFromFile

std::error_code CreateBlankFSQPipeline() noexcept {
  IRIS_LOG_ENTER();
  VkResult result;
  Pipeline pipeline;

  VkPipelineLayoutCreateInfo plci = {};
  plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

  result = vkCreatePipelineLayout(sDevice, &plci, nullptr, &pipeline.layout);
  if (result != VK_SUCCESS) {
    GetLogger()->error("Cannot create pipeline layout: {}", to_string(result));
    IRIS_LOG_LEAVE();
    return make_error_code(result);
  }

  std::array<VkPipelineShaderStageCreateInfo, 2> stages{{
    {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
     VK_SHADER_STAGE_VERTEX_BIT, VK_NULL_HANDLE, "main", nullptr},
    {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
     VK_SHADER_STAGE_FRAGMENT_BIT, VK_NULL_HANDLE, "main", nullptr},
  }};

  if (auto module = CreateShaderFromSource(sBlankFSQVertexShaderSource,
                                           VK_SHADER_STAGE_VERTEX_BIT)) {
    stages[0].module = *module;
  } else {
    IRIS_LOG_LEAVE();
    return module.error();
  }

  if (auto module = CreateShaderFromSource(sBlankFSQFragmentShaderSource,
                                           VK_SHADER_STAGE_FRAGMENT_BIT)) {
    stages[1].module = *module;
  } else {
    IRIS_LOG_LEAVE();
    return module.error();
  }

  VkPipelineVertexInputStateCreateInfo vertexInputState = {};
  vertexInputState.sType =
    VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

  VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = {};
  inputAssemblyState.sType =
    VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  inputAssemblyState.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  VkPipelineViewportStateCreateInfo viewportState = {};
  viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewportState.viewportCount = 1;
  viewportState.pViewports = nullptr;
  viewportState.scissorCount = 1;
  viewportState.pScissors = nullptr;

  VkPipelineRasterizationStateCreateInfo rasterizationState = {};
  rasterizationState.sType =
    VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterizationState.depthClampEnable = VK_FALSE;
  rasterizationState.rasterizerDiscardEnable = VK_FALSE;
  rasterizationState.polygonMode = VK_POLYGON_MODE_FILL;
  rasterizationState.cullMode = VK_CULL_MODE_FRONT_BIT;
  rasterizationState.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rasterizationState.depthBiasEnable = VK_FALSE;
  rasterizationState.lineWidth = 1.f;

  VkPipelineMultisampleStateCreateInfo multisampleState = {};
  multisampleState.sType =
    VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisampleState.rasterizationSamples = sSurfaceSampleCount;
  multisampleState.sampleShadingEnable = VK_FALSE;
  multisampleState.minSampleShading = 1.f;

  VkPipelineDepthStencilStateCreateInfo depthStencilState = {};
  depthStencilState.sType =
    VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depthStencilState.depthTestEnable = VK_TRUE;
  depthStencilState.depthWriteEnable = VK_TRUE;
  depthStencilState.depthCompareOp = VK_COMPARE_OP_LESS;
  depthStencilState.depthBoundsTestEnable = VK_FALSE;
  depthStencilState.stencilTestEnable = VK_FALSE;

  VkPipelineColorBlendAttachmentState colorBlendStateAttachment = {};
  colorBlendStateAttachment.colorWriteMask =
    VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
    VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  colorBlendStateAttachment.blendEnable = VK_FALSE;

  VkPipelineColorBlendStateCreateInfo colorBlendState = {};
  colorBlendState.sType =
    VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlendState.logicOpEnable = VK_FALSE;
  colorBlendState.logicOp = VK_LOGIC_OP_COPY;
  colorBlendState.attachmentCount = 1;
  colorBlendState.pAttachments = &colorBlendStateAttachment;

  std::array<VkDynamicState, 2> dynamicStates{
    {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR}};

  VkPipelineDynamicStateCreateInfo dynamicState = {};
  dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamicState.dynamicStateCount =
    gsl::narrow_cast<std::uint32_t>(dynamicStates.size());
  dynamicState.pDynamicStates = dynamicStates.data();

  VkGraphicsPipelineCreateInfo gpci = {};
  gpci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  gpci.stageCount = gsl::narrow_cast<std::uint32_t>(stages.size());
  gpci.pStages = stages.data();
  gpci.pVertexInputState = &vertexInputState;
  gpci.pInputAssemblyState = &inputAssemblyState;
  gpci.pViewportState = &viewportState;
  gpci.pRasterizationState = &rasterizationState;
  gpci.pMultisampleState = &multisampleState;
  gpci.pDepthStencilState = &depthStencilState;
  gpci.pColorBlendState = &colorBlendState;
  gpci.pDynamicState = &dynamicState;
  gpci.layout = pipeline.layout;
  gpci.renderPass = sRenderPass;
  gpci.subpass = 0;

  result = vkCreateGraphicsPipelines(sDevice, VK_NULL_HANDLE, 1, &gpci, nullptr,
                                     &pipeline.pipeline);
  if (result != VK_SUCCESS) {
    GetLogger()->error("Cannot create graphics pipeline: {}",
                       to_string(result));
    IRIS_LOG_LEAVE();
    return make_error_code(result);
  }

  vkDestroyShaderModule(sDevice, stages[0].module, nullptr);
  vkDestroyShaderModule(sDevice, stages[1].module, nullptr);

  NameObject(VK_OBJECT_TYPE_PIPELINE_LAYOUT, pipeline.layout,
             "sBlankFSQPipelineLayout");
  NameObject(VK_OBJECT_TYPE_PIPELINE, pipeline.pipeline, "sBlankFSQPipeline");
  Pipelines().insert(std::make_pair("BlankFSQ", std::move(pipeline)));

  IRIS_LOG_LEAVE();
  return VulkanResult::kSuccess;
} // CreateBlankFSQPipeline

} // namespace iris::Renderer

std::error_code
iris::Renderer::Initialize(gsl::czstring<> appName, Options const& options,
                           std::uint32_t appVersion,
                           spdlog::sinks_init_list logSinks) noexcept {
  GetLogger(logSinks);
  IRIS_LOG_ENTER();

  GOOGLE_PROTOBUF_VERIFY_VERSION;

  ////
  // In order to reduce the verbosity of the Vulakn API, initialization occurs
  // over several sub-functions below. Each function is called in-order and
  // assumes the previous functions have all be called.
  ////

  if (sInitialized) {
    IRIS_LOG_LEAVE();
    return Error::kAlreadyInitialized;
  }

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
#if defined(VK_USE_PLATFORM_XLIB_KHR) // plus the platform-specific surface
    VK_KHR_XLIB_SURFACE_EXTENSION_NAME,
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
  physicalDeviceFeatures.features.fillModeNonSolid = VK_TRUE;
  physicalDeviceFeatures.features.multiViewport = VK_TRUE;
  physicalDeviceFeatures.features.pipelineStatisticsQuery = VK_TRUE;

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
                       Options::kReportDebugMessages)) {
    IRIS_LOG_LEAVE();
    return Error::kInitializationFailed;
  }

  if ((options & Options::kReportDebugMessages) ==
      Options::kReportDebugMessages) {
    CreateDebugUtilsMessenger(); // ignore any returned error
  }

  FindDeviceGroup();

  if (auto error = ChoosePhysicalDevice(physicalDeviceFeatures,
                                        physicalDeviceExtensionNames)) {
    IRIS_LOG_LEAVE();
    return Error::kInitializationFailed;
  }

  if (auto error = CreateDeviceAndQueues(physicalDeviceFeatures,
                                         physicalDeviceExtensionNames)) {
    IRIS_LOG_LEAVE();
    return Error::kInitializationFailed;
  }

  if (auto error = CreateCommandPool()) {
    IRIS_LOG_LEAVE();
    return Error::kInitializationFailed;
  }

  if (auto error = CreateFencesAndSemaphores()) {
    IRIS_LOG_LEAVE();
    return Error::kInitializationFailed;
  }

  if (auto error = CreateAllocator()) {
    IRIS_LOG_LEAVE();
    return Error::kInitializationFailed;
  }

  if (auto error = CreateRenderPass()) {
    IRIS_LOG_LEAVE();
    return Error::kInitializationFailed;
  }

  if (auto error = AllocateCommandBuffers()) {
    IRIS_LOG_LEAVE();
    return Error::kInitializationFailed;
  }

  if (auto error = CreateBlankFSQPipeline()) {
    IRIS_LOG_LEAVE();
    return Error::kInitializationFailed;
  }

  sInitialized = true;
  sRunning = true;

  IRIS_LOG_LEAVE();
  return Error::kNone;
} // iris::Renderer::Initialize

void iris::Renderer::Shutdown() noexcept {
  IRIS_LOG_ENTER();

  // Drain the queue of all tasks
  TaskResult taskResult;

  try {
    while (sTasksResultsQueue.try_pop(taskResult)) {
      std::visit(
        [](auto&& arg) {
          using T = std::decay_t<decltype(arg)>;
          if constexpr (std::is_same_v<T, std::error_code>) {
            GetLogger()->error("Task result has error: {}", arg.message());
          } else if constexpr (std::is_same_v<T, Control::Control>) {
            Control(arg);
          }
        },
        taskResult);
    }
  } catch (std::exception const& e) {
    GetLogger()->critical(
      "Exception encountered while processing task results: {}", e.what());
    std::terminate();
  }

  vkQueueWaitIdle(sGraphicsCommandQueue);
  vkDeviceWaitIdle(sDevice);
  Windows().clear();

  vkFreeCommandBuffers(sDevice, sGraphicsCommandPool,
                       gsl::narrow_cast<std::uint32_t>(sCommandBuffers.size()),
                       sCommandBuffers.data());

  if (sRenderPass != VK_NULL_HANDLE) {
    vkDestroyRenderPass(sDevice, sRenderPass, nullptr);
  }

  Pipelines().clear();

  if (sAllocator != VK_NULL_HANDLE) { vmaDestroyAllocator(sAllocator); }

  if (sImagesReadyForPresent != VK_NULL_HANDLE) {
    vkDestroySemaphore(sDevice, sImagesReadyForPresent, nullptr);
  }

  if (sFrameComplete != VK_NULL_HANDLE) {
    vkDestroyFence(sDevice, sFrameComplete, nullptr);
  }

  if (sOneTimeSubmit != VK_NULL_HANDLE) {
    vkDestroyFence(sDevice, sOneTimeSubmit, nullptr);
  }

  if (sGraphicsCommandPool != VK_NULL_HANDLE) {
    vkDestroyCommandPool(sDevice, sGraphicsCommandPool, nullptr);
  }

  if (sDevice != VK_NULL_HANDLE) { vkDestroyDevice(sDevice, nullptr); }

  if (sDebugUtilsMessenger != VK_NULL_HANDLE) {
    vkDestroyDebugUtilsMessengerEXT(sInstance, sDebugUtilsMessenger, nullptr);
  }

  if (sInstance != VK_NULL_HANDLE) { vkDestroyInstance(sInstance, nullptr); }

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

void iris::Renderer::Frame() noexcept {
  TaskResult taskResult;

  try {
    while (sTasksResultsQueue.try_pop(taskResult)) {
      std::visit(
        [](auto&& arg) {
          using T = std::decay_t<decltype(arg)>;
          if constexpr (std::is_same_v<T, std::error_code>) {
            GetLogger()->error("Task result has error: {}", arg.message());
          } else if constexpr (std::is_same_v<T, Control::Control>) {
            Control(arg);
          }
        },
        taskResult);
    }
  } catch (std::exception const& e) {
    GetLogger()->critical(
      "Exception encountered while processing task results: {}", e.what());
    std::terminate();
  }

  if (!sRunning) return;

  auto&& windows = Windows();
  if (windows.empty()) return;

  for (auto&& iter : windows) {
    auto&& window = iter.second;
    if (window.resized) {
      window.surface.Resize(window.window.Extent());
      window.resized = false;
    }
  }

  for (auto&& iter : windows) iter.second.Frame();

  const std::size_t numWindows = windows.size();
  absl::FixedArray<std::uint32_t> imageIndices(numWindows);
  absl::FixedArray<VkExtent2D> extents(numWindows);
  absl::FixedArray<VkViewport> viewports(numWindows);
  absl::FixedArray<VkRect2D> scissors(numWindows);
  absl::FixedArray<VkClearColorValue> clearColors(numWindows);
  absl::FixedArray<VkFramebuffer> framebuffers(numWindows);
  absl::FixedArray<VkImage> images(numWindows);
  absl::FixedArray<VkSemaphore> waitSemaphores(numWindows);
  absl::FixedArray<VkSwapchainKHR> swapchains(numWindows);

  VkResult result;

  result = vkWaitForFences(sDevice, 1, &sFrameComplete, VK_TRUE, UINT64_MAX);
  if (result != VK_SUCCESS) {
    GetLogger()->error("Error waiting on fence: {}", to_string(result));
    return;
  }

  result = vkResetFences(sDevice, 1, &sFrameComplete);
  if (result != VK_SUCCESS) {
    GetLogger()->error("Error resetting fence: {}", to_string(result));
    return;
  }

  result = vkResetCommandPool(sDevice, sGraphicsCommandPool, 0);
  if (result != VK_SUCCESS) {
    GetLogger()->error("Error resetting command pool: {}", to_string(result));
    return;
  }

  //
  // Acquire images/semaphores from all iris::Window objects
  //

  std::size_t i = 0;
  for (auto&& iter : windows) {
    auto&& window = iter.second;
    result = vkAcquireNextImageKHR(sDevice, window.surface.swapchain,
                                   UINT64_MAX, window.surface.imageAvailable,
                                   VK_NULL_HANDLE, &imageIndices[i]);

    if (result == VK_SUBOPTIMAL_KHR || result == VK_ERROR_OUT_OF_DATE_KHR) {
      GetLogger()->warn("Swapchains out of date; resizing and re-acquiring");
      window.surface.Resize(window.window.Extent());
      window.resized = false;

      result = vkAcquireNextImageKHR(sDevice, window.surface.swapchain,
                                     UINT64_MAX, window.surface.imageAvailable,
                                     VK_NULL_HANDLE, &imageIndices[i]);
    }

    if (result != VK_SUCCESS) {
      GetLogger()->error(
        "Renderer::Frame: acquiring next image for {} failed: {}", iter.first,
        to_string(result));
      return;
    }

    extents[i] = window.surface.extent;
    viewports[i] = window.surface.viewport;
    scissors[i] = window.surface.scissor;
    clearColors[i] = window.surface.clearColor;
    framebuffers[i] = window.surface.framebuffers[imageIndices[i]];
    images[i] = window.surface.colorImages[imageIndices[i]];
    waitSemaphores[i] = window.surface.imageAvailable;
    swapchains[i] = window.surface.swapchain;

    i += 1;
  }

  //
  // Build command buffers (or use pre-recorded ones)
  //

  sCommandBufferIndex = (sCommandBufferIndex + 1) % sCommandBuffers.size();
  auto&& cb = sCommandBuffers[sCommandBufferIndex];

  VkCommandBufferBeginInfo cbi = {};
  cbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  cbi.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

  result = vkBeginCommandBuffer(cb, &cbi);
  if (result != VK_SUCCESS) {
    GetLogger()->error("Error beginning command buffer: {}", to_string(result));
    return;
  }

  absl::FixedArray<VkClearValue> clearValues(sNumRenderPassAttachments);
  clearValues[1].depthStencil = {1.f, 0};

  VkRenderPassBeginInfo rbi = {};
  rbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  rbi.renderPass = sRenderPass;
  rbi.clearValueCount =
    gsl::narrow_cast<std::uint32_t>(sNumRenderPassAttachments);

  Pipeline& blankFSQ = Pipelines()["BlankFSQ"];

  for (std::size_t j = 0; j < numWindows; ++j) {
    clearValues[0].color = clearColors[j];
    rbi.renderArea.extent = extents[j];
    rbi.framebuffer = framebuffers[j];
    rbi.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(cb, &rbi, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdSetViewport(cb, 0, 1, &viewports[j]);
    vkCmdSetScissor(cb, 0, 1, &scissors[j]);

    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, blankFSQ.pipeline);
    vkCmdDraw(cb, 3, 1, 0, 0);

    vkCmdEndRenderPass(cb);
  }

  result = vkEndCommandBuffer(cb);
  if (result != VK_SUCCESS) {
    GetLogger()->error("Error ending command buffer: {}", to_string(result));
    return;
  }

  //
  // Submit command buffers to a queue, waiting on all acquired image
  // semaphores and signaling a single frameFinished semaphore
  //

  absl::FixedArray<VkPipelineStageFlags> waitDstStages(numWindows);
  std::fill_n(waitDstStages.begin(), numWindows,
              VK_PIPELINE_STAGE_TRANSFER_BIT);

  VkSubmitInfo si = {};
  si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  si.waitSemaphoreCount = gsl::narrow_cast<std::uint32_t>(numWindows);
  si.pWaitSemaphores = waitSemaphores.data();
  si.pWaitDstStageMask = waitDstStages.data();
  si.commandBufferCount = 1;
  si.pCommandBuffers = &cb;
  si.signalSemaphoreCount = 1;
  si.pSignalSemaphores = &sImagesReadyForPresent;

  result = vkQueueSubmit(sGraphicsCommandQueue, 1, &si, sFrameComplete);
  if (result != VK_SUCCESS) {
    GetLogger()->error("Error submitting command buffer: {}",
                       to_string(result));
    return;
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

  result = vkQueuePresentKHR(sGraphicsCommandQueue, &pi);
  if (result != VK_SUCCESS) {
    GetLogger()->error("Error presenting swapchains: {}", to_string(result));
    return;
  }
} // iris::Renderer::Frame

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
      auto const& bg = windowMessage.background();

      Window::Options options = Window::Options::kNone;
      if (windowMessage.decoration()) options |= Window::Options::kDecorated;
      if (windowMessage.stereo()) options |= Window::Options::kStereo;

      if (auto win =
            Window::Create(windowMessage.name().c_str(),
                           {windowMessage.x(), windowMessage.y()},
                           {windowMessage.width(), windowMessage.height()},
                           {bg.r(), bg.g(), bg.b(), bg.a()}, options,
                           windowMessage.display())) {
        Windows().emplace(windowMessage.name(), std::move(*win));
      }
    }
    break;
  case iris::Control::Control_Type_WINDOW: {
    auto&& windowMessage = controlMessage.window();
    auto const& bg = windowMessage.background();

    Window::Options options = Window::Options::kNone;
    if (windowMessage.decoration()) options |= Window::Options::kDecorated;
    if (windowMessage.stereo()) options |= Window::Options::kStereo;

    if (auto win = Window::Create(
          windowMessage.name().c_str(), {windowMessage.x(), windowMessage.y()},
          {windowMessage.width(), windowMessage.height()},
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

tl::expected<std::vector<VkCommandBuffer>, std::error_code>
iris::Renderer::AllocateCommandBuffers(std::uint32_t count,
                                       VkCommandBufferLevel level) noexcept {
  IRIS_LOG_ENTER();
  VkResult result;

  VkCommandBufferAllocateInfo commandBufferAI = {};
  commandBufferAI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  commandBufferAI.commandPool = sGraphicsCommandPool;
  commandBufferAI.level = level;
  commandBufferAI.commandBufferCount = count;

  std::vector<VkCommandBuffer> commandBuffers(count);
  result =
    vkAllocateCommandBuffers(sDevice, &commandBufferAI, commandBuffers.data());
  if (result != VK_SUCCESS) {
    GetLogger()->error("Unable to allocate command buffers: {}",
                       to_string(result));
    IRIS_LOG_LEAVE();
    return tl::unexpected(make_error_code(result));
  }

  IRIS_LOG_LEAVE();
  return commandBuffers;
} // iris::Renderer::AllocateCommandBuffers

void iris::Renderer::FreeCommandBuffers(
  std::vector<VkCommandBuffer>& commandBuffers) noexcept {
  IRIS_LOG_ENTER();
  vkFreeCommandBuffers(sDevice, sGraphicsCommandPool,
                       gsl::narrow_cast<std::uint32_t>(commandBuffers.size()),
                       commandBuffers.data());
  commandBuffers.clear();
  IRIS_LOG_LEAVE();
} // iris::Renderer::FreeCommandBuffers

tl::expected<VkCommandBuffer, std::error_code>
iris::Renderer::BeginOneTimeSubmit() noexcept {
  IRIS_LOG_ENTER();
  VkResult result;
  VkCommandBuffer commandBuffer;

  if (auto cbs = AllocateCommandBuffers(1)) {
    commandBuffer = (*cbs)[0];
  } else {
    return tl::unexpected(cbs.error());
  }

  VkCommandBufferBeginInfo commandBufferBI = {};
  commandBufferBI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  commandBufferBI.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

  result = vkBeginCommandBuffer(commandBuffer, &commandBufferBI);
  if (result != VK_SUCCESS) {
    vkFreeCommandBuffers(sDevice, sGraphicsCommandPool, 1, &commandBuffer);
    GetLogger()->error("Cannot begin one time submit command buffer: {}",
                       to_string(result));
    return tl::unexpected(make_error_code(result));
  }

  IRIS_LOG_LEAVE();
  return commandBuffer;
} // iris::Renderer::BeginOneTimeSubmit

std::error_code
iris::Renderer::EndOneTimeSubmit(VkCommandBuffer commandBuffer) noexcept {
  IRIS_LOG_ENTER();
  VkResult result;

  VkSubmitInfo submit = {};
  submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &commandBuffer;

  result = vkEndCommandBuffer(commandBuffer);
  if (result != VK_SUCCESS) {
    GetLogger()->error("Cannot end one time submit command buffer: {}",
                       to_string(result));
    goto fail;
  }

  result = vkQueueSubmit(sGraphicsCommandQueue, 1, &submit, sOneTimeSubmit);
  if (result != VK_SUCCESS) {
    GetLogger()->error("Cannot submit command buffer: {}", to_string(result));
    goto fail;
  }

  result = vkWaitForFences(sDevice, 1, &sOneTimeSubmit, VK_TRUE, UINT64_MAX);
  if (result != VK_SUCCESS) {
    GetLogger()->error("Cannot wait on one time submit fence: {}",
                       to_string(result));
    goto fail;
  }

  result = vkResetFences(sDevice, 1, &sOneTimeSubmit);
  if (result != VK_SUCCESS) {
    GetLogger()->error("Cannot reset one time submit fence: {}",
                       to_string(result));
    goto fail;
  }

  result = VK_SUCCESS;
fail:
  vkFreeCommandBuffers(sDevice, sGraphicsCommandPool, 1, &commandBuffer);
  IRIS_LOG_LEAVE();
  return make_error_code(result);
} // iris::Renderer::EndOneTimeSubmit

std::error_code
iris::Renderer::TransitionImage(VkImage image, VkImageLayout oldLayout,
                                VkImageLayout newLayout,
                                std::uint32_t mipLevels) noexcept {
  IRIS_LOG_ENTER();

  VkImageMemoryBarrier barrier = {};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.oldLayout = oldLayout;
  barrier.newLayout = newLayout;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image;
  barrier.subresourceRange.baseMipLevel = 0;
  barrier.subresourceRange.levelCount = mipLevels;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = 1;

  if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    // FIXME: handle stencil
  } else {
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  }

  VkPipelineStageFlagBits srcStage;
  VkPipelineStageFlagBits dstStage;

  if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
      newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
             newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    dstStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
             newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  } else {
    GetLogger()->critical("Logic error: unsupported layout transition");
    std::terminate();
  }

  VkCommandBuffer commandBuffer;
  if (auto cb = BeginOneTimeSubmit()) {
    commandBuffer = *cb;
  } else {
    return cb.error();
  }

  vkCmdPipelineBarrier(commandBuffer, srcStage, dstStage, 0, 0, nullptr, 0,
                       nullptr, 1, &barrier);

  if (auto error = EndOneTimeSubmit(commandBuffer)) {
    return error;
  }

  IRIS_LOG_LEAVE();
  return VulkanResult::kSuccess;
} // iris::Renderer::TransitionImage
