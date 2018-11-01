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

std::error_code TransitionImage(VkImage image, VkImageLayout oldLayout,
                                VkImageLayout newLayout,
                                std::uint32_t mipLevels = 1) noexcept;

} // namespace iris::Renderer

#endif // HEV_IRIS_RENDERER_IMPL_H_

