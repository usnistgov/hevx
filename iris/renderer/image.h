#ifndef HEV_IRIS_RENDERER_IMAGE_H_
#define HEV_IRIS_RENDERER_IMAGE_H_

#include "renderer/vulkan.h"
#include "tl/expected.hpp"
#include <system_error>

namespace iris::Renderer {

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

tl::expected<std::pair<VkImage, VmaAllocation>, std::error_code>
CreateImageFromMemory(VkImageType imageType, VkFormat format, VkExtent3D extent,
                      VkImageUsageFlags usage, VmaMemoryUsage memoryUsage,
                      unsigned char* pixels,
                      std::uint32_t bytes_per_pixel) noexcept;

std::error_code TransitionImage(VkImage image, VkImageLayout oldLayout,
                                VkImageLayout newLayout,
                                std::uint32_t mipLevels = 1) noexcept;

} // namespace iris::Renderer

#endif // HEV_IRIS_RENDERER_IMAGE_H_

