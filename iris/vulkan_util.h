#ifndef HEV_IRIS_VULKAN_UTIL_H_
#define HEV_IRIS_VULKAN_UTIL_H_

#include "iris/config.h"

#include "absl/container/fixed_array.h"
#if STD_FS_IS_EXPERIMENTAL
#include <experimental/filesystem>
namespace filesystem = std::experimental::filesystem;
#else
#include <filesystem>
namespace filesystem = std::filesystem;
#endif
#include "expected.hpp"
#include "gsl/gsl"
#include "iris/vulkan.h"
#include <cstdint>
#include <exception>
#include <string>
#include <system_error>
#include <tuple>

namespace iris::Renderer {

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
[[nodiscard]] tl::expected<VkInstance, std::system_error> CreateInstance(
  gsl::czstring<> appName, std::uint32_t appVersion,
  gsl::span<gsl::czstring<>> extensionNames,
  gsl::span<gsl::czstring<>> layerNames,
  PFN_vkDebugUtilsMessengerCallbackEXT debugUtilsMessengerCallback =
    nullptr) noexcept;

[[nodiscard]] tl::expected<VkDebugUtilsMessengerEXT, std::system_error>
CreateDebugUtilsMessenger(
  VkInstance instance,
  PFN_vkDebugUtilsMessengerCallbackEXT debugUtilsMessengerCallback) noexcept;

/*! \brief Compare two VkPhysicalDeviceFeatures2 structures.
 *
 * \see
 * https://www.khronos.org/registry/vulkan/specs/1.1-extensions/html/vkspec.html#features-features
 */
[[nodiscard]] bool
ComparePhysicalDeviceFeatures(VkPhysicalDeviceFeatures2 a,
                              VkPhysicalDeviceFeatures2 b) noexcept;

[[nodiscard]] tl::expected<std::uint32_t, std::system_error>
GetQueueFamilyIndex(VkPhysicalDevice physicalDevice,
                    VkQueueFlags queueFlags) noexcept;

tl::expected<void, std::system_error>
DumpPhysicalDevice(VkPhysicalDevice physicalDevice,
                   char const* indent = "") noexcept;

tl::expected<void, std::system_error>
DumpPhysicalDevices(VkInstance instance) noexcept;

/*! \brief Check if a specific physical device meets specified requirements.
 *
 * \see
 * https://www.khronos.org/registry/vulkan/specs/1.1-extensions/html/vkspec.html#devsandqueues-physical-device-enumeration
 */
[[nodiscard]] tl::expected<bool, std::system_error> IsPhysicalDeviceGood(
  VkPhysicalDevice physicalDevice, VkPhysicalDeviceFeatures2 features,
  gsl::span<gsl::czstring<>> extensionNames, VkQueueFlags queueFlags) noexcept;

/*! \brief Choose the Vulkan physical device.
 *
 * \see
 * https://www.khronos.org/registry/vulkan/specs/1.1-extensions/html/vkspec.html#devsandqueues-physical-device-enumeration
 */
[[nodiscard]] tl::expected<VkPhysicalDevice, std::system_error>
ChoosePhysicalDevice(VkInstance instance, VkPhysicalDeviceFeatures2 features,
                     gsl::span<gsl::czstring<>> extensionNames,
                     VkQueueFlags queueFlags) noexcept;

/*! \brief Create the Vulkan logical device.
 *
 * \see
 * https://www.khronos.org/registry/vulkan/specs/1.1-extensions/html/vkspec.html#devsandqueues-devices
 * \see
 * https://www.khronos.org/registry/vulkan/specs/1.1-extensions/html/vkspec.html#devsandqueues-queues
 */
[[nodiscard]] tl::expected<std::tuple<VkDevice, std::uint32_t>,
                           std::system_error>
CreateDevice(VkPhysicalDevice physicalDevice,
             VkPhysicalDeviceFeatures2 physicalDeviceFeatures,
             gsl::span<gsl::czstring<>> extensionNames,
             std::uint32_t queueFamilyIndex) noexcept;

[[nodiscard]] tl::expected<VmaAllocator, std::system_error>
CreateAllocator(VkPhysicalDevice physicalDevice, VkDevice device) noexcept;

[[nodiscard]] tl::expected<absl::FixedArray<VkSurfaceFormatKHR>,
                           std::system_error>
GetPhysicalDeviceSurfaceFormats(VkPhysicalDevice physicalDevice,
                                VkSurfaceKHR surface);

//! \brief Convert a VkPhysicalDeviceType to a std::string
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

//! \brief Convert a VkQueueFlags to a std::string
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

//! \brief Convert a VkDebugUtilsMessageTypeFlagsEXT to a std::string
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

} // namespace iris::Renderer

#endif // HEV_IRIS_VULKAN_UTIL_H_
