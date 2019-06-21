#ifndef HEV_IRIS_ACCELERATION_STRUCTURE_H_
#define HEV_IRIS_ACCELERATION_STRUCTURE_H_

#include "iris/config.h"

#include "iris/buffer.h"
#include "iris/shader.h"
#include "iris/vulkan.h"

#if PLATFORM_COMPILER_MSVC
#include <codeanalysis/warnings.h>
#pragma warning(push)
#pragma warning(disable: ALL_CODE_ANALYSIS_WARNINGS)
#pragma warning(disable: ALL_CPPCORECHECK_WARNINGS)
#endif

#include "expected.hpp"
#include "gsl/gsl"
#include <cstdint>
#include <system_error>

#if PLATFORM_COMPILER_MSVC
#pragma warning(pop)
#endif

namespace iris {

struct AccelerationStructure {
  VkAccelerationStructureNV structure{VK_NULL_HANDLE};
  VmaAllocation allocation{VK_NULL_HANDLE};
  std::uint64_t handle{UINT64_MAX};

  explicit operator bool() const noexcept {
    return structure != VK_NULL_HANDLE && allocation != VK_NULL_HANDLE;
  }
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
    std::memset(transform, 0, sizeof(float) * 12);
    transform[0] = transform[4] = transform[8] = 1.f;
    customIndex = 0;
    mask = 0xF;
    offset = 0;
    flags = 0;
  }
}; // GeometryInstance

[[nodiscard]] tl::expected<AccelerationStructure, std::system_error>
CreateAccelerationStructure(std::uint32_t instanceCount,
                            VkDeviceSize compactedSize) noexcept;

[[nodiscard]] tl::expected<AccelerationStructure, std::system_error>
CreateAccelerationStructure(gsl::span<VkGeometryNV> geometries,
                            VkDeviceSize compactedSize) noexcept;

[[nodiscard]] tl::expected<void, std::system_error> BuildAccelerationStructure(
  AccelerationStructure const& structure, VkCommandPool commandPool,
  VkQueue queue, VkFence fence, gsl::span<GeometryInstance> instances) noexcept;

[[nodiscard]] tl::expected<void, std::system_error> BuildAccelerationStructure(
  AccelerationStructure const& structure, VkCommandPool commandPool,
  VkQueue queue, VkFence fence, gsl::span<VkGeometryNV> geometries) noexcept;

void DestroyAccelerationStructure(AccelerationStructure structure) noexcept;

} // namespace iris

#endif // HEV_IRIS_ACCELERATION_STRUCTURE_H_

