#ifndef HEV_IRIS_IMAGE_H_
#define HEV_IRIS_IMAGE_H_

#include "iris/config.h"

#include "iris/vulkan_util.h"

#if PLATFORM_COMPILER_MSVC
#include <codeanalysis/warnings.h>
#pragma warning(push)
#pragma warning(disable: ALL_CODE_ANALYSIS_WARNINGS)
#pragma warning(disable: ALL_CPPCORECHECK_WARNINGS)
#endif

#include "expected.hpp"
#include <system_error>

#if PLATFORM_COMPILER_MSVC
#pragma warning(pop)
#endif

namespace iris {

struct Image {
  VkImage image{VK_NULL_HANDLE};
  VmaAllocation allocation{VK_NULL_HANDLE};

  explicit operator bool() const noexcept {
    return image != VK_NULL_HANDLE && allocation != VK_NULL_HANDLE;
  }
}; // struct Image

inline void SetImageLayout(VkCommandBuffer commandBuffer, Image image,
                           VkPipelineStageFlags srcStages,
                           VkPipelineStageFlags dstStages,
                           VkImageLayout oldLayout, VkImageLayout newLayout,
                           VkImageAspectFlags aspectMask,
                           std::uint32_t mipLevels,
                           std::uint32_t arrayLayers) noexcept {
  vk::SetImageLayout(commandBuffer, image.image, srcStages, dstStages,
                     oldLayout, newLayout, aspectMask, mipLevels, arrayLayers);
}

tl::expected<void, std::system_error>
TransitionImage(VkCommandPool commandPool, VkQueue queue, VkFence fence,
                Image image, VkImageLayout oldLayout, VkImageLayout newLayout,
                std::uint32_t mipLevels, std::uint32_t arrayLayers) noexcept;

[[nodiscard]] tl::expected<Image, std::system_error>
AllocateImage(VkFormat format, VkExtent2D extent, std::uint32_t mipLevels,
              std::uint32_t arrayLayers, VkSampleCountFlagBits sampleCount,
              VkImageUsageFlags imageUsage, VkImageTiling imageTiling,
              VmaMemoryUsage memoryUsage) noexcept;

[[nodiscard]] tl::expected<VkImageView, std::system_error>
CreateImageView(Image image, VkImageViewType type, VkFormat format,
                VkImageSubresourceRange subresourceRange) noexcept;

[[nodiscard]] tl::expected<Image, std::system_error>
CreateImage(VkCommandPool commandPool, VkQueue queue, VkFence fence,
            VkFormat format, VkExtent2D extent, VkImageUsageFlags imageUsage,
            VmaMemoryUsage memoryUsage, gsl::not_null<std::byte*> pixels,
            std::uint32_t bytesPerPixel) noexcept;

[[nodiscard]] tl::expected<Image, std::system_error>
CreateImage(VkCommandPool commandPool, VkQueue queue, VkFence fence,
            VkFormat format, gsl::span<VkExtent2D> extents,
            VkImageUsageFlags imageUsage, VmaMemoryUsage memoryUsage,
            gsl::not_null<std::byte*> levelsPixels,
            std::uint32_t bytesPerPixel) noexcept;

void DestroyImage(Image image) noexcept;

} // namespace iris

#endif // HEV_IRIS_IMAGE_H_

