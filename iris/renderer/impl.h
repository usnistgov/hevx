#ifndef HEV_IRIS_RENDERER_IMPL_H_
#define HEV_IRIS_RENDERER_IMPL_H_

#include "config.h"
#include "error.h"
#include "renderer/renderer.h"
#include "renderer/vulkan.h"
#include <cstdint>

namespace iris::Renderer {

extern VkInstance sInstance;
extern VkPhysicalDevice sPhysicalDevice;
extern std::uint32_t sGraphicsQueueFamilyIndex;
extern VkDevice sDevice;
extern VkQueue sGraphicsCommandQueue;
extern VkCommandPool sGraphicsCommandPool;
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

tl::expected<std::vector<VkCommandBuffer>, std::error_code>
AllocateCommandBuffers(
  std::uint32_t count,
  VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY) noexcept;
void FreeCommandBuffers(std::vector<VkCommandBuffer>& commandBuffers) noexcept;

tl::expected<VkCommandBuffer, std::error_code> BeginOneTimeSubmit() noexcept;
std::error_code EndOneTimeSubmit(VkCommandBuffer commandBuffer) noexcept;

tl::expected<VkImageView, std::error_code> CreateImageView(
  VkImage image, VkFormat format, VkImageViewType viewType,
  VkImageSubresourceRange imageSubresourceRange,
  VkComponentMapping componentMapping = {
    VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
    VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY}) noexcept;

tl::expected<std::tuple<VkImage, VmaAllocation, VkImageView>, std::error_code>
CreateImageAndView(VkImageType imageType, VkFormat format, VkExtent3D extent,
                   std::uint32_t mipLevels, std::uint32_t arrayLayers,
                   VkSampleCountFlagBits samples, VkImageUsageFlags usage,
                   VmaMemoryUsage memoryUsage, VkImageViewType viewType,
                   VkImageSubresourceRange imageSubresourceRange,
                   VkComponentMapping componentMapping = {
                     VK_COMPONENT_SWIZZLE_IDENTITY,
                     VK_COMPONENT_SWIZZLE_IDENTITY,
                     VK_COMPONENT_SWIZZLE_IDENTITY,
                     VK_COMPONENT_SWIZZLE_IDENTITY}) noexcept;

std::error_code TransitionImage(VkImage image, VkImageLayout oldLayout,
                                VkImageLayout newLayout,
                                std::uint32_t mipLevels = 1) noexcept;

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

