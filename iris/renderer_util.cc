#include "renderer_util.h"
#if PLATFORM_COMPILER_GCC
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#include "absl/container/inlined_vector.h"
#include "enumerate.h"
#include "error.h"
#include "renderer.h"
#include "logging.h"
#include "vulkan_util.h"

tl::expected<iris::Renderer::AccelerationStructure, std::system_error>
iris::Renderer::CreateAccelerationStructure(
  VkAccelerationStructureInfoNV const& accelerationStructureInfo,
  VkDeviceSize compactedSize) noexcept {
  IRIS_LOG_ENTER();
  Expects(sDevice != VK_NULL_HANDLE);
  Expects(sAllocator != VK_NULL_HANDLE);

  VkAccelerationStructureCreateInfoNV accelerationStructureCI = {};
  accelerationStructureCI.sType =
    VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_NV;
  accelerationStructureCI.compactedSize = compactedSize;
  accelerationStructureCI.info = accelerationStructureInfo;

  AccelerationStructure structure;
  if (auto result = vkCreateAccelerationStructureNV(
        sDevice, &accelerationStructureCI, nullptr, &structure.structure);
      result != VK_SUCCESS) {
    return tl::unexpected(std::system_error(
      make_error_code(result), "Cannot create acceleration structure"));
  }

  VkMemoryRequirements2KHR memoryRequirements = {};
  memoryRequirements.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2_KHR;

  VkAccelerationStructureMemoryRequirementsInfoNV memoryRequirementsInfo = {};
  memoryRequirementsInfo.sType =
    VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_NV;
  memoryRequirementsInfo.accelerationStructure = structure.structure;
  memoryRequirementsInfo.type =
    VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_OBJECT_NV;
  vkGetAccelerationStructureMemoryRequirementsNV(
    sDevice, &memoryRequirementsInfo, &memoryRequirements);

  VmaAllocationCreateInfo allocationCI = {};
  allocationCI.flags = VMA_MEMORY_USAGE_GPU_ONLY;
  allocationCI.memoryTypeBits =
    memoryRequirements.memoryRequirements.memoryTypeBits;

  if (auto result =
        vmaAllocateMemory(sAllocator, &memoryRequirements.memoryRequirements,
                          &allocationCI, &structure.allocation, nullptr);
      result != VK_SUCCESS) {
    vkDestroyAccelerationStructureNV(sDevice, structure.structure, nullptr);
    return tl::unexpected(std::system_error(iris::make_error_code(result),
                                            "Cannot allocate memory"));
  }

  VmaAllocationInfo allocationInfo;
  vmaGetAllocationInfo(sAllocator, structure.allocation, &allocationInfo);

  VkBindAccelerationStructureMemoryInfoNV bindAccelerationStructureMemoryInfo =
    {};
  bindAccelerationStructureMemoryInfo.sType =
    VK_STRUCTURE_TYPE_BIND_ACCELERATION_STRUCTURE_MEMORY_INFO_NV;
  bindAccelerationStructureMemoryInfo.accelerationStructure =
    structure.structure;
  bindAccelerationStructureMemoryInfo.memory = allocationInfo.deviceMemory;
  bindAccelerationStructureMemoryInfo.memoryOffset = 0;

  if (auto result = vkBindAccelerationStructureMemoryNV(
        sDevice, 1, &bindAccelerationStructureMemoryInfo);
      result != VK_SUCCESS) {
    vmaFreeMemory(sAllocator, structure.allocation);
    vkDestroyAccelerationStructureNV(sDevice, structure.structure, nullptr);
    return tl::unexpected(std::system_error(
      make_error_code(result), "Cannot bind memory to acceleration structure"));
  }

  Ensures(structure.structure != VK_NULL_HANDLE);
  Ensures(structure.allocation != VK_NULL_HANDLE);

  IRIS_LOG_LEAVE();
  return structure;
} // iris::Renderer::CreateAccelerationStructure

