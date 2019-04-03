#ifndef HEV_IRIS_IMAGE_H_
#define HEV_IRIS_IMAGE_H_

#include "iris/vulkan.h"

namespace iris {

struct Image {
  VkImage image{VK_NULL_HANDLE};
  VmaAllocation allocation{VK_NULL_HANDLE};
}; // struct Image

tl::expected<void, std::system_error>
TransitionImage(VkCommandPool commandPool, VkQueue queue, VkFence fence,
                VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
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

void DestroyImage(Image image) noexcept;

} // namespace iris

#endif // HEV_IRIS_IMAGE_H_

