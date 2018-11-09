#ifndef HEV_IRIS_RENDERER_IMPL_H_
#define HEV_IRIS_RENDERER_IMPL_H_

#include "absl/container/fixed_array.h"
#include "config.h"
#include "error.h"
#include "gsl/gsl"
#include "renderer/renderer.h"
#include "renderer/vulkan.h"
#include <cstdint>
#include <system_error>
#include <vector>

namespace iris::Renderer {

extern VkInstance sInstance;
extern VkPhysicalDevice sPhysicalDevice;
extern std::uint32_t sGraphicsQueueFamilyIndex;
extern VkDevice sDevice;
extern VkQueue sGraphicsCommandQueue;
extern VkFence sFrameComplete;
extern VmaAllocator sAllocator;

extern VkSurfaceFormatKHR sSurfaceColorFormat;
extern VkFormat sSurfaceDepthStencilFormat;
extern VkSampleCountFlagBits sSurfaceSampleCount;
extern VkPresentModeKHR sSurfacePresentMode;

extern std::uint32_t sNumRenderPassAttachments;
extern std::uint32_t sColorTargetAttachmentIndex;
extern std::uint32_t sColorResolveAttachmentIndex;
extern std::uint32_t sDepthStencilTargetAttachmentIndex;
extern std::uint32_t sDepthStencilResolveAttachmentIndex;
extern VkRenderPass sRenderPass;

tl::expected<std::vector<VkCommandBuffer>, std::system_error>
AllocateCommandBuffers(
  std::uint32_t count,
  VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY) noexcept;
void FreeCommandBuffers(std::vector<VkCommandBuffer>& commandBuffers) noexcept;

tl::expected<VkCommandBuffer, std::system_error> BeginOneTimeSubmit() noexcept;
tl::expected<void, std::system_error>
EndOneTimeSubmit(VkCommandBuffer commandBuffer) noexcept;

tl::expected<std::pair<VkDescriptorSetLayout, std::vector<VkDescriptorSet>>,
             std::system_error>
CreateDescriptors(gsl::span<VkDescriptorSetLayoutBinding> bindings) noexcept;

inline void UpdateDescriptorSets(
  gsl::span<VkWriteDescriptorSet> writeDescriptorSets,
  gsl::span<VkCopyDescriptorSet> copyDescriptorSets = {}) noexcept {
  vkUpdateDescriptorSets(sDevice,
                         static_cast<uint32_t>(writeDescriptorSets.size()),
                         writeDescriptorSets.data(),
                         static_cast<uint32_t>(copyDescriptorSets.size()),
                         copyDescriptorSets.data());
} // UpdateDescriptorSets


inline tl::expected<void*, std::system_error>
MapMemory(VmaAllocation allocation) noexcept {
  void* ptr;
  if (auto result = vmaMapMemory(sAllocator, allocation, &ptr);
      result != VK_SUCCESS) {
    return tl::unexpected(
      std::system_error(make_error_code(result), "Cannot map memory"));
  }
  return ptr;
} // MapMemory

inline void UnmapMemory(VmaAllocation allocation, VkDeviceSize flushOffset = 0,
                        VkDeviceSize flushSize = 0) {
  if (flushSize > 0) {
    vmaFlushAllocation(sAllocator, allocation, flushOffset, flushSize);
  }
  vmaUnmapMemory(sAllocator, allocation);
} // UnmapMemory

template <class T>
void NameObject(VkObjectType objectType, T objectHandle,
                gsl::czstring<> objectName) noexcept {
  VkDebugUtilsObjectNameInfoEXT objectNameInfo = {
    VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT, nullptr, objectType,
    reinterpret_cast<std::uint64_t>(objectHandle), objectName};
  vkSetDebugUtilsObjectNameEXT(sDevice, &objectNameInfo);
} // NameObject

} // namespace iris::Renderer

#endif // HEV_IRIS_RENDERER_IMPL_H_

