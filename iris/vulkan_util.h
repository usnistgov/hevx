#ifndef HEV_IRIS_VULKAN_UTIL_H_
#define HEV_IRIS_VULKAN_UTIL_H_

#include "iris/config.h"

#include "iris/types.h"
#include "iris/vulkan.h"

#if PLATFORM_COMPILER_MSVC
#include <codeanalysis/warnings.h>
#pragma warning(push)
#pragma warning(disable : ALL_CODE_ANALYSIS_WARNINGS)
#pragma warning(disable : ALL_CPPCORECHECK_WARNINGS)
#endif

#include "absl/container/inlined_vector.h"
#include "gsl/gsl"
#include <cstdint>
#include <exception>
#include <string>
#include <system_error>

#if PLATFORM_COMPILER_MSVC
#pragma warning(pop)
#endif

namespace iris::vk {

/*! \brief Create a Vulkan Instance.

\see
https://www.khronos.org/registry/vulkan/specs/1.1-extensions/html/vkspec.html#initialization-instances
\see
https://www.khronos.org/registry/vulkan/specs/1.1-extensions/html/vkspec.html#extended-functionality-extensions
\see
https://www.khronos.org/registry/vulkan/specs/1.1-extensions/html/vkspec.html#extensions
\see
https://vulkan.lunarg.com/doc/sdk/1.1.82.1/windows/layer_configuration.html
*/
[[nodiscard]] expected<VkInstance, std::system_error> CreateInstance(
  gsl::czstring<> appName, std::uint32_t appVersion,
  gsl::span<gsl::czstring<>> extensionNames,
  gsl::span<gsl::czstring<>> layerNames,
  PFN_vkDebugUtilsMessengerCallbackEXT debugUtilsMessengerCallback =
    nullptr) noexcept;

[[nodiscard]] expected<VkDebugUtilsMessengerEXT, std::system_error>
CreateDebugUtilsMessenger(
  VkInstance instance,
  PFN_vkDebugUtilsMessengerCallbackEXT debugUtilsMessengerCallback) noexcept;

/*! \brief Compare two VkPhysicalDeviceFeatures2 structures.

\see
https://www.khronos.org/registry/vulkan/specs/1.1-extensions/html/vkspec.html#features-features
*/
[[nodiscard]] bool
ComparePhysicalDeviceFeatures(VkPhysicalDeviceFeatures2 a,
                              VkPhysicalDeviceFeatures2 b) noexcept;

[[nodiscard]] expected<std::uint32_t, std::system_error>
GetQueueFamilyIndex(VkPhysicalDevice physicalDevice,
                    VkQueueFlags queueFlags) noexcept;

expected<void, std::system_error>
DumpPhysicalDevice(VkPhysicalDevice physicalDevice,
                   char const* indent = "") noexcept;

expected<void, std::system_error>
DumpPhysicalDevices(VkInstance instance) noexcept;

/*! \brief Check if a specific physical device meets specified requirements.

\see
https://www.khronos.org/registry/vulkan/specs/1.1-extensions/html/vkspec.html#devsandqueues-physical-device-enumeration
*/
[[nodiscard]] expected<bool, std::system_error> IsPhysicalDeviceGood(
  VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures2 features,
  gsl::span<gsl::czstring<>> extensionNames, VkQueueFlags queueFlags) noexcept;

/*! \brief Choose the Vulkan physical device.

\see
https://www.khronos.org/registry/vulkan/specs/1.1-extensions/html/vkspec.html#devsandqueues-physical-device-enumeration
*/
[[nodiscard]] expected<VkPhysicalDevice, std::system_error>
ChoosePhysicalDevice(VkInstance instance, VkPhysicalDeviceFeatures2 features,
                     gsl::span<gsl::czstring<>> requiredExtensionNames,
                     gsl::span<gsl::czstring<>> optionalExtensionNames,
                     VkQueueFlags queueFlags) noexcept;

/*! \brief Create the Vulkan logical device.

\see
https://www.khronos.org/registry/vulkan/specs/1.1-extensions/html/vkspec.html#devsandqueues-devices
\see
https://www.khronos.org/registry/vulkan/specs/1.1-extensions/html/vkspec.html#devsandqueues-queues
*/
[[nodiscard]] expected<std::pair<VkDevice, std::uint32_t>, std::system_error>
CreateDevice(VkPhysicalDevice physicalDevice,
             VkPhysicalDeviceFeatures2 physicalDeviceFeatures,
             gsl::span<gsl::czstring<>> extensionNames,
             std::uint32_t queueFamilyIndex) noexcept;

[[nodiscard]] expected<VmaAllocator, std::system_error>
CreateAllocator(VkPhysicalDevice physicalDevice, VkDevice device) noexcept;

[[nodiscard]] expected<absl::InlinedVector<VkSurfaceFormatKHR, 128>,
                       std::system_error>
GetPhysicalDeviceSurfaceFormats(VkPhysicalDevice physicalDevice,
                                VkSurfaceKHR surface);

void SetImageLayout(VkCommandBuffer commandBuffer, VkImage image,
                    VkPipelineStageFlags srcStages,
                    VkPipelineStageFlags dstStages, VkImageLayout oldLayout,
                    VkImageLayout newLayout, VkImageAspectFlags aspectMask,
                    std::uint32_t mipLevels,
                    std::uint32_t arrayLayers) noexcept;

inline void BeginDebugLabel(VkCommandBuffer commandBuffer,
                            char const* name) noexcept {
  VkDebugUtilsLabelEXT label = {VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
                                nullptr,
                                name,
                                {0.f, 0.f, 0.f, 0.f}};
  vkCmdBeginDebugUtilsLabelEXT(commandBuffer, &label);
} // BeginDebugLabel

inline void EndDebugLabel(VkCommandBuffer commandBuffer) noexcept {
  vkCmdEndDebugUtilsLabelEXT(commandBuffer);
} // EndDebugLabel

inline void BeginDebugLabel(VkQueue queue, char const* name) noexcept {
  VkDebugUtilsLabelEXT label = {VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
                                nullptr,
                                name,
                                {0.f, 0.f, 0.f, 0.f}};
  vkQueueBeginDebugUtilsLabelEXT(queue, &label);
}

inline void EndDebugLabel(VkQueue queue) noexcept {
  vkQueueEndDebugUtilsLabelEXT(queue);
}

/*!
\brief Convert a VkPhysicalDeviceType to a std::string.
\return a std::string of a VkPhysicalDeviceType.
*/
inline std::string to_string(VkPhysicalDeviceType type) noexcept {
  using namespace std::string_literals;
  switch (type) {
  case VK_PHYSICAL_DEVICE_TYPE_OTHER: return "Other"s;
  case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "IntegratedGPU"s;
  case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return "DiscreteGPU"s;
  case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return "VirtualGPU"s;
  case VK_PHYSICAL_DEVICE_TYPE_CPU: return "CPU"s;
  }
  return "unknown"s;
}

/*!
\brief Convert a VkQueueFlags to a std::string.
\return a std::string of a VkQueueFlags.
*/
inline std::string to_string(VkQueueFlagBits flags) noexcept {
  using namespace std::string_literals;
  if (!flags) return "{}"s;
  std::string result;
  if (flags & VK_QUEUE_GRAPHICS_BIT) result += "Graphics | ";
  if (flags & VK_QUEUE_COMPUTE_BIT) result += "Compute | ";
  if (flags & VK_QUEUE_TRANSFER_BIT) result += "Transfer | ";
  if (flags & VK_QUEUE_SPARSE_BINDING_BIT) result += "SparseBinding | ";
  if (flags & VK_QUEUE_PROTECTED_BIT) result += "Protected | ";
  return "{" + result.substr(0, result.size() - 3) + "}";
}

/*!
\brief Convert a VkDebugUtilsMessageTypeFlagsEXT to a std::string
\return a std::string of a VkDebugUtilsMessageTypeFlagsEXT
*/
inline std::string
to_string(VkDebugUtilsMessageTypeFlagBitsEXT types) noexcept {
  using namespace std::string_literals;
  if (!types) return "{}"s;
  std::string result;
  if (types & VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT) {
    result += "General | ";
  }
  if (types & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) {
    result += "Validation | ";
  }
  if (types & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) {
    result += "Performance | ";
  }
  return "{" + result.substr(0, result.size() - 3) + "}";
}

} // namespace iris::vk

#endif // HEV_IRIS_VULKAN_UTIL_H_
