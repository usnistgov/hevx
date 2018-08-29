#ifndef HEV_IRIS_HELPERS_H_
#define HEV_IRIS_HELPERS_H_
/*! \file
 * \brief Small helper functions.
 */

#include "flextVk.h"
#include <string>

namespace iris::Renderer {

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
inline std::string to_string(VkQueueFlags flags) noexcept {
  if (!flags) return "{}";
  std::string result;
  if (flags & VK_QUEUE_GRAPHICS_BIT) result += "Graphics | ";
  if (flags & VK_QUEUE_COMPUTE_BIT) result += "Compute | ";
  if (flags & VK_QUEUE_TRANSFER_BIT) result += "Transfer | ";
  if (flags & VK_QUEUE_SPARSE_BINDING_BIT) result += "SparseBinding | ";
  if (flags & VK_QUEUE_PROTECTED_BIT) result += "Protected | ";
  return "{" + result.substr(0, result.size() - 3) + "}";
}

} // namespace iris::Renderer

#endif // HEV_IRIS_HELPERS_H_

