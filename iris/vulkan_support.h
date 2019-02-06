#ifndef HEV_IRIS_VULKAN_SUPPORT_H_
#define HEV_IRIS_VULKAN_SUPPORT_H_

#include "expected.hpp"
#include "gsl/gsl"
#include "iris/config.h"
#include "iris/flextVk.h"
#include <cstddef>
#include <cstdint>
#include <exception>
#include <string>
#include <system_error>

// FIXME: vma needs these, flextGL should probably generate them
using PFN_vkGetPhysicalDeviceProperties =
  decltype(vkGetPhysicalDeviceProperties);
using PFN_vkGetPhysicalDeviceMemoryProperties =
  decltype(vkGetPhysicalDeviceMemoryProperties);
using PFN_vkAllocateMemory = decltype(vkAllocateMemory);
using PFN_vkFreeMemory = decltype(vkFreeMemory);
using PFN_vkMapMemory = decltype(vkMapMemory);
using PFN_vkUnmapMemory = decltype(vkUnmapMemory);
using PFN_vkFlushMappedMemoryRanges = decltype(vkFlushMappedMemoryRanges);
using PFN_vkInvalidateMappedMemoryRanges =
  decltype(vkInvalidateMappedMemoryRanges);
using PFN_vkBindBufferMemory = decltype(vkBindBufferMemory);
using PFN_vkBindImageMemory = decltype(vkBindImageMemory);
using PFN_vkGetBufferMemoryRequirements =
  decltype(vkGetBufferMemoryRequirements);
using PFN_vkGetImageMemoryRequirements = decltype(vkGetImageMemoryRequirements);
using PFN_vkCreateBuffer = decltype(vkCreateBuffer);
using PFN_vkDestroyBuffer = decltype(vkDestroyBuffer);
using PFN_vkCreateImage = decltype(vkCreateImage);
using PFN_vkDestroyImage = decltype(vkDestroyImage);
using PFN_vkCmdCopyBuffer = decltype(vkCmdCopyBuffer);

#if PLATFORM_COMPILER_MSVC
#if defined(NOMINMAX)
#undef NOMINMAX // vk_mem_alloc.h unconditionally defines this
#endif
#endif // PLATFORM_COMPILER_MSVC
#include "vk_mem_alloc.h"
#if PLATFORM_COMPILER_MSVC
#if !defined(NOMINMAX)
#define NOMINMAX // vk_mem_alloc.h unconditionally defines this
#endif
#endif // PLATFORM_COMPILER_MSVC

namespace iris::Renderer {

struct Image {
  VkImage image{VK_NULL_HANDLE};
  VmaAllocation allocation{VK_NULL_HANDLE};
  VkImageView view{VK_NULL_HANDLE};

  constexpr Image() noexcept = default;
  Image(Image const&) = delete;
  Image& operator=(Image const&) = delete;
  ~Image() noexcept = default;

  Image(Image&& other) noexcept
    : image(other.image)
    , allocation(other.allocation)
    , view(other.view) {
    other.image = VK_NULL_HANDLE;
    other.allocation = VK_NULL_HANDLE;
    other.view = VK_NULL_HANDLE;
  }

  Image& operator=(Image&& rhs) noexcept {
    if (this == &rhs) return *this;
    image = rhs.image;
    allocation = rhs.allocation;
    view = rhs.view;
    rhs.image = VK_NULL_HANDLE;
    rhs.allocation = VK_NULL_HANDLE;
    rhs.view = VK_NULL_HANDLE;
    return *this;
  }
}; // struct Image

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
               bool reportDebug) noexcept;

[[nodiscard]] tl::expected<VkDebugUtilsMessengerEXT, std::system_error>
CreateDebugUtilsMessenger(VkInstance instance) noexcept;

[[nodiscard]] tl::expected<std::uint32_t, std::system_error>
GetQueueFamilyIndex(VkPhysicalDevice physicalDevice,
                    VkQueueFlags queueFlags) noexcept;

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
[[nodiscard]] tl::expected<VkDevice, std::system_error>
CreateDevice(VkPhysicalDevice physicalDevice,
             VkPhysicalDeviceFeatures2 physicalDeviceFeatures,
             gsl::span<gsl::czstring<>> extensionNames,
             std::uint32_t queueFamilyIndex) noexcept;

[[nodiscard]] tl::expected<VmaAllocator, std::system_error>
CreateAllocator(VkPhysicalDevice physicalDevice, VkDevice device) noexcept;

void DumpPhysicalDevice(VkPhysicalDevice device, std::size_t index,
                        int indentAmount = 0) noexcept;

/*! \brief Compare two VkPhysicalDeviceFeatures2 structures.
 *
 * \see
 * https://www.khronos.org/registry/vulkan/specs/1.1-extensions/html/vkspec.html#features-features
 */
[[nodiscard]] bool
ComparePhysicalDeviceFeatures(VkPhysicalDeviceFeatures2 a,
                              VkPhysicalDeviceFeatures2 b) noexcept;

} // namespace iris::Renderer

namespace iris {

//! \brief Vulkan result codes.
enum class VulkanResult {
  kSuccess = VK_SUCCESS,
  kNotReady = VK_NOT_READY,
  kTimeout = VK_TIMEOUT,
  kEventSet = VK_EVENT_SET,
  kEventReset = VK_EVENT_RESET,
  kIncomplete = VK_INCOMPLETE,
  kErrorOutOfHostMemory = VK_ERROR_OUT_OF_HOST_MEMORY,
  kErrorOutOfDeviceMemory = VK_ERROR_OUT_OF_DEVICE_MEMORY,
  kErrorInitializationFailed = VK_ERROR_INITIALIZATION_FAILED,
  kErrorDeviceLost = VK_ERROR_DEVICE_LOST,
  kErrorMemoryMapFailed = VK_ERROR_MEMORY_MAP_FAILED,
  kErrorLayerNotPresent = VK_ERROR_LAYER_NOT_PRESENT,
  kErrorExtensionNotPresent = VK_ERROR_EXTENSION_NOT_PRESENT,
  kErrorFeatureNotPresent = VK_ERROR_FEATURE_NOT_PRESENT,
  kErrorIncompatibleDriver = VK_ERROR_INCOMPATIBLE_DRIVER,
  kErrorTooManyObjects = VK_ERROR_TOO_MANY_OBJECTS,
  kErrorFormatNotSupported = VK_ERROR_FORMAT_NOT_SUPPORTED,
  kErrorFragmentedPool = VK_ERROR_FRAGMENTED_POOL,
  kErrorOutOfPoolMemory = VK_ERROR_OUT_OF_POOL_MEMORY,
  kErrorInvalidExternalHandle = VK_ERROR_INVALID_EXTERNAL_HANDLE,
  kErrorSurfaceLostKHR = VK_ERROR_SURFACE_LOST_KHR,
  kErrorNativeWindowInUseKHR = VK_ERROR_NATIVE_WINDOW_IN_USE_KHR,
  kSuboptimalKHR = VK_SUBOPTIMAL_KHR,
  kErrorOutOfDataKHR = VK_ERROR_OUT_OF_DATE_KHR,
  kErrorValidationFailedEXT = VK_ERROR_VALIDATION_FAILED_EXT,
}; // enum class VulkanResult
static_assert(VK_SUCCESS == 0);

//! \brief Convert a VulkanResult to a std::string.
inline std::string to_string(VulkanResult result) noexcept {
  using namespace std::string_literals;
  switch (result) {
  case VulkanResult::kSuccess: return "success"s;
  case VulkanResult::kNotReady: return "not ready"s;
  case VulkanResult::kTimeout: return "timeout"s;
  case VulkanResult::kEventSet: return "event set"s;
  case VulkanResult::kEventReset: return "event reset"s;
  case VulkanResult::kIncomplete: return "incomplete"s;
  case VulkanResult::kErrorOutOfHostMemory: return "error: out of host memory"s;
  case VulkanResult::kErrorOutOfDeviceMemory:
    return "error: out of device memory"s;
  case VulkanResult::kErrorInitializationFailed:
    return "error: initialization failed"s;
  case VulkanResult::kErrorDeviceLost: return "error: device lost"s;
  case VulkanResult::kErrorMemoryMapFailed: return "error: memory map failed"s;
  case VulkanResult::kErrorLayerNotPresent: return "error: layer not present"s;
  case VulkanResult::kErrorExtensionNotPresent:
    return "error: extension not present"s;
  case VulkanResult::kErrorFeatureNotPresent:
    return "error: feature not present"s;
  case VulkanResult::kErrorIncompatibleDriver:
    return "error: incompatible driver"s;
  case VulkanResult::kErrorTooManyObjects: return "error: too many objects"s;
  case VulkanResult::kErrorFormatNotSupported:
    return "error: format not supported"s;
  case VulkanResult::kErrorFragmentedPool: return "error: fragmented pool"s;
  case VulkanResult::kErrorOutOfPoolMemory: return "error: out of pool memory"s;
  case VulkanResult::kErrorInvalidExternalHandle:
    return "error: invalid external handle"s;
  case VulkanResult::kErrorSurfaceLostKHR: return "error: surface lost"s;
  case VulkanResult::kErrorNativeWindowInUseKHR:
    return "error: native window in use"s;
  case VulkanResult::kSuboptimalKHR: return "suboptimal"s;
  case VulkanResult::kErrorOutOfDataKHR: return "error: out of date"s;
  case VulkanResult::kErrorValidationFailedEXT:
    return "error: validation failed"s;
  }
  return "unknown"s;
} // to_string

//! \brief Convert a VkResult to a std::string.
inline std::string to_string(VkResult result) noexcept {
  return to_string(static_cast<VulkanResult>(result));
}

//! \brief Implements std::error_category for \ref VulkanResult
class VulkanResultCategory : public std::error_category {
public:
  virtual ~VulkanResultCategory() noexcept {}

  //! \brief Get the name of this category.
  virtual const char* name() const noexcept override {
    return "iris::VulkanResult";
  }

  //! \brief Convert an int representing an Error into a std::string.
  virtual std::string message(int ev) const override {
    return to_string(static_cast<VulkanResult>(ev));
  }
};

//! The global instance of the VulkanResultCategory.
inline VulkanResultCategory const gVulkanResultCategory;

/*! \brief Get the global instance of the VulkanResultCategory.
 * \return \ref gVulkanResultCategory
 */
inline std::error_category const& GetVulkanResultCategory() {
  return gVulkanResultCategory;
}

/*! \brief Make a std::error_code from a \ref VulkanResult.
 * \return std::error_code
 */
inline std::error_code make_error_code(VulkanResult r) noexcept {
  return std::error_code(static_cast<int>(r), GetVulkanResultCategory());
}

/*! \brief Make a std::error_code from a VkResult.
 * \return std::error_code
 */
inline std::error_code make_error_code(VkResult r) noexcept {
  return std::error_code(static_cast<int>(r), GetVulkanResultCategory());
}

} // namespace iris

namespace std {

template <>
struct is_error_code_enum<iris::VulkanResult> : public true_type {};

} // namespace std

namespace iris {

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

} // namespace iris

#endif // HEV_IRIS_RENDERER_VULKAN_H_
