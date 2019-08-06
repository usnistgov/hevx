#include "acceleration_structure.h"
#include "config.h"

#include "error.h"
#include "logging.h"
#include "renderer.h"
#include "renderer_private.h"

namespace iris {

static expected<iris::AccelerationStructure, std::system_error>
CreateAccelerationStructure(VkAccelerationStructureInfoNV const& info,
                            VkDeviceSize compactedSize) noexcept {
  IRIS_LOG_ENTER();
  Expects(Renderer::sDevice != VK_NULL_HANDLE);
  Expects(Renderer::sAllocator != VK_NULL_HANDLE);

  AccelerationStructure structure;

  VkAccelerationStructureCreateInfoNV asCI = {};
  asCI.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_NV;
  asCI.compactedSize = compactedSize;
  asCI.info = info;

  if (auto result = vkCreateAccelerationStructureNV(
        Renderer::sDevice, &asCI, nullptr, &structure.structure);
      result != VK_SUCCESS) {
    return unexpected(std::system_error(
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
    Renderer::sDevice, &memoryRequirementsInfo, &memoryRequirements);

  VmaAllocationCreateInfo allocationCI = {};
  allocationCI.flags = VMA_MEMORY_USAGE_GPU_ONLY;
  allocationCI.memoryTypeBits =
    memoryRequirements.memoryRequirements.memoryTypeBits;

  if (auto result = vmaAllocateMemory(
        Renderer::sAllocator, &memoryRequirements.memoryRequirements,
        &allocationCI, &structure.allocation, nullptr);
      result != VK_SUCCESS) {
    vkDestroyAccelerationStructureNV(Renderer::sDevice, structure.structure,
                                     nullptr);
    return unexpected(std::system_error(iris::make_error_code(result),
                                        "Cannot allocate memory"));
  }

  VmaAllocationInfo allocationInfo;
  vmaGetAllocationInfo(Renderer::sAllocator, structure.allocation,
                       &allocationInfo);

  VkBindAccelerationStructureMemoryInfoNV bindAccelerationStructureMemoryInfo =
    {};
  bindAccelerationStructureMemoryInfo.sType =
    VK_STRUCTURE_TYPE_BIND_ACCELERATION_STRUCTURE_MEMORY_INFO_NV;
  bindAccelerationStructureMemoryInfo.accelerationStructure =
    structure.structure;
  bindAccelerationStructureMemoryInfo.memory = allocationInfo.deviceMemory;
  bindAccelerationStructureMemoryInfo.memoryOffset = 0;

  if (auto result = vkBindAccelerationStructureMemoryNV(
        Renderer::sDevice, 1, &bindAccelerationStructureMemoryInfo);
      result != VK_SUCCESS) {
    vmaFreeMemory(Renderer::sAllocator, structure.allocation);
    vkDestroyAccelerationStructureNV(Renderer::sDevice, structure.structure,
                                     nullptr);
    return unexpected(std::system_error(
      make_error_code(result), "Cannot bind memory to acceleration structure"));
  }

  if (auto result = vkGetAccelerationStructureHandleNV(
        iris::Renderer::sDevice, structure.structure, sizeof(structure.handle),
        &structure.handle);
      result != VK_SUCCESS) {
    vmaFreeMemory(Renderer::sAllocator, structure.allocation);
    vkDestroyAccelerationStructureNV(Renderer::sDevice, structure.structure,
                                     nullptr);
    return unexpected(std::system_error(
      make_error_code(result), "Cannot get acceleration structure handle"));
  }

  Ensures(structure.structure != VK_NULL_HANDLE);
  Ensures(structure.allocation != VK_NULL_HANDLE);
  Ensures(structure.handle < UINT64_MAX);

  IRIS_LOG_LEAVE();
  return structure;
} // CreateAccelerationStructure

static expected<void, std::system_error> BuildAccelerationStructure(
  AccelerationStructure const& structure, VkCommandPool commandPool,
  VkQueue queue, VkFence fence, VkAccelerationStructureInfoNV const& info,
  VkBuffer instanceData) noexcept {
  IRIS_LOG_ENTER();
  Expects(Renderer::sDevice != VK_NULL_HANDLE);
  Expects(structure.structure != VK_NULL_HANDLE);

  VkMemoryRequirements2KHR memoryRequirements = {};
  memoryRequirements.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2_KHR;

  VkAccelerationStructureMemoryRequirementsInfoNV memoryRequirementsInfo = {};
  memoryRequirementsInfo.sType =
    VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_NV;
  memoryRequirementsInfo.accelerationStructure = structure.structure;
  memoryRequirementsInfo.type =
    VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_BUILD_SCRATCH_NV;
  vkGetAccelerationStructureMemoryRequirementsNV(
    Renderer::sDevice, &memoryRequirementsInfo, &memoryRequirements);

  auto scratch = AllocateBuffer(memoryRequirements.memoryRequirements.size,
                                VK_BUFFER_USAGE_RAY_TRACING_BIT_NV,
                                VMA_MEMORY_USAGE_GPU_ONLY);
  if (!scratch) {
    using namespace std::string_literals;
    IRIS_LOG_LEAVE();
    return unexpected(std::system_error(
      scratch.error().code(),
      "Cannot allocate acceleration structure build scratch memory: "s +
        scratch.error().what()));
  }

  auto commandBuffer = Renderer::BeginOneTimeSubmit(commandPool);
  if (!commandBuffer) {
    DestroyBuffer(*scratch);
    IRIS_LOG_LEAVE();
    return unexpected(std::system_error(commandBuffer.error()));
  }

  vkCmdBuildAccelerationStructureNV(
    *commandBuffer,      // commandBuffer
    &info,               // pInfo (VkAccelerationStructureInfoNV const*)
    instanceData,        // instanceData
    0,                   // instanceOffset
    VK_FALSE,            // update
    structure.structure, // dst
    VK_NULL_HANDLE,      // src
    scratch->buffer,     // scratchBuffer
    0                    // scratchOffset
  );

  if (auto result = iris::Renderer::EndOneTimeSubmit(*commandBuffer,
                                                     commandPool, queue, fence);
      !result) {
    using namespace std::string_literals;
    DestroyBuffer(*scratch);
    IRIS_LOG_LEAVE();
    return unexpected(std::system_error(
      result.error().code(),
      "Cannot build acceleration struture: "s + result.error().what()));
  }

  DestroyBuffer(*scratch);
  IRIS_LOG_LEAVE();
  return {};
} // BuildAccelerationStructure

} // namespace iris

iris::expected<iris::AccelerationStructure, std::system_error>
iris::CreateAccelerationStructure(std::uint32_t instanceCount,
                                  VkDeviceSize compactedSize) noexcept {
  IRIS_LOG_ENTER();

  VkAccelerationStructureInfoNV asInfo = {};
  asInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_INFO_NV;
  asInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_NV;
  asInfo.instanceCount = instanceCount;
  asInfo.geometryCount = 0;
  asInfo.pGeometries = nullptr;

  auto ret = CreateAccelerationStructure(asInfo, compactedSize);
  IRIS_LOG_LEAVE();
  return ret;
} // iris::CreateAccelerationStructure

iris::expected<iris::AccelerationStructure, std::system_error>
iris::CreateAccelerationStructure(gsl::span<VkGeometryNV> geometries,
                                  VkDeviceSize compactedSize) noexcept {
  IRIS_LOG_ENTER();

  VkAccelerationStructureInfoNV asInfo = {};
  asInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_INFO_NV;
  asInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_NV;
  asInfo.flags = 0;
  asInfo.instanceCount = 0;
  asInfo.geometryCount = gsl::narrow_cast<std::uint32_t>(geometries.size());
  asInfo.pGeometries = geometries.data();

  auto ret = CreateAccelerationStructure(asInfo, compactedSize);
  IRIS_LOG_LEAVE();
  return ret;
} // iris::CreateAccelerationStructure

iris::expected<void, std::system_error> iris::BuildAccelerationStructure(
  AccelerationStructure const& structure, VkCommandPool commandPool,
  VkQueue queue, VkFence fence,
  gsl::span<GeometryInstance> instances) noexcept {
  IRIS_LOG_ENTER();

  auto instanceBuffer = iris::AllocateBuffer(
    instances.size() * sizeof(iris::GeometryInstance),
    VK_BUFFER_USAGE_RAY_TRACING_BIT_NV, VMA_MEMORY_USAGE_CPU_TO_GPU);
  if (!instanceBuffer) {
    IRIS_LOG_LEAVE();
    return unexpected(instanceBuffer.error());
  }

  if (auto ptr = instanceBuffer->Map<iris::GeometryInstance*>()) {
    std::memcpy(*ptr, instances.data(),
                instances.size() * sizeof(GeometryInstance));
    instanceBuffer->Unmap();
  } else {
    DestroyBuffer(*instanceBuffer);
    IRIS_LOG_LEAVE();
    return unexpected(ptr.error());
  }

  VkAccelerationStructureInfoNV asInfo = {};
  asInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_INFO_NV;
  asInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_NV;
  asInfo.instanceCount = gsl::narrow_cast<std::uint32_t>(instances.size());
  asInfo.geometryCount = 0;
  asInfo.pGeometries = nullptr;

  auto ret = BuildAccelerationStructure(structure, commandPool, queue, fence,
                                        asInfo, instanceBuffer->buffer);

  DestroyBuffer(*instanceBuffer);
  IRIS_LOG_LEAVE();
  return ret;
} // iris::BuildAccelerationStructure

iris::expected<void, std::system_error> iris::BuildAccelerationStructure(
  AccelerationStructure const& structure, VkCommandPool commandPool,
  VkQueue queue, VkFence fence, gsl::span<VkGeometryNV> geometries) noexcept {
  IRIS_LOG_ENTER();

  VkAccelerationStructureInfoNV asInfo = {};
  asInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_INFO_NV;
  asInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_NV;
  asInfo.flags = 0;
  asInfo.instanceCount = 0;
  asInfo.geometryCount = gsl::narrow_cast<std::uint32_t>(geometries.size());
  asInfo.pGeometries = geometries.data();

  auto ret = BuildAccelerationStructure(structure, commandPool, queue, fence,
                                        asInfo, VK_NULL_HANDLE);
  IRIS_LOG_LEAVE();
  return ret;
} // iris::BuildAccelerationStructure

void iris::DestroyAccelerationStructure(
  iris::AccelerationStructure structure) noexcept {
  IRIS_LOG_ENTER();

  vmaFreeMemory(Renderer::sAllocator, structure.allocation);
  vkDestroyAccelerationStructureNV(Renderer::sDevice, structure.structure,
                                   nullptr);

  IRIS_LOG_LEAVE();
} // iris::DestroyAccelerationStructure
