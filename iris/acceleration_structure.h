#ifndef HEV_IRIS_ACCELERATION_STRUCTURE_H_
#define HEV_IRIS_ACCELERATION_STRUCTURE_H_

#include "expected.hpp"
#include "gsl/gsl"
#include "iris/buffer.h"
#include "iris/shader.h"
#include "iris/vulkan.h"
#include <cstdint>
#include <system_error>

namespace iris {

struct AccelerationStructure {
  VkAccelerationStructureNV structure{VK_NULL_HANDLE};
  VmaAllocation allocation{VK_NULL_HANDLE};
  std::uint64_t handle{UINT64_MAX};
  VkAccelerationStructureInfoNV info{};
}; // struct AccelerationStructure

struct GeometryInstance {
  float transform[12];
  std::uint32_t customIndex : 24;
  std::uint32_t mask : 8;
  std::uint32_t offset : 24;
  std::uint32_t flags : 8;
  std::uint64_t accelerationStructureHandle;

  GeometryInstance(std::uint64_t handle = 0) noexcept
    : accelerationStructureHandle(handle) {
    customIndex = 0;
    mask = 0xF;
    offset = 0;
    flags = 0;
  }
}; // GeometryInstance

[[nodiscard]] tl::expected<AccelerationStructure, std::system_error>
CreateAccelerationStructure(VkAccelerationStructureInfoNV info,
                            VkDeviceSize compactedSize) noexcept;

[[nodiscard]] tl::expected<AccelerationStructure, std::system_error>
CreateAccelerationStructure(std::uint32_t instanceCount,
                            VkDeviceSize compactedSize) noexcept;

[[nodiscard]] tl::expected<AccelerationStructure, std::system_error>
CreateAccelerationStructure(gsl::span<VkGeometryNV> geometries,
                            VkDeviceSize compactedSize) noexcept;

tl::expected<void, std::system_error>
BuildAccelerationStructure(AccelerationStructure const& structure,
                           VkCommandPool commandPool, VkQueue queue,
                           VkFence fence,
                           VkBuffer instanceData = VK_NULL_HANDLE) noexcept;

} // namespace iris

#endif // HEV_IRIS_ACCELERATION_STRUCTURE_H_

