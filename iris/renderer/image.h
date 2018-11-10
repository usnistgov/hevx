#ifndef HEV_IRIS_RENDERER_IMAGE_H_
#define HEV_IRIS_RENDERER_IMAGE_H_

#include "iris/renderer/impl.h"
#include <cstdint>
#include <string>
#include <system_error>

namespace iris::Renderer {

struct ImageView {
  static tl::expected<ImageView, std::system_error> Create(
    VkImage image, VkFormat format, VkImageViewType type,
    VkImageSubresourceRange imageSubresourceRange, std::string name = {},
    VkComponentMapping componentMapping = {
      VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
      VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY}) noexcept;

  VkImageViewType type;
  VkFormat format{VK_FORMAT_UNDEFINED};
  VkImageView handle{VK_NULL_HANDLE};

  operator VkImageView() const noexcept { return handle; }

  ImageView() = default;
  ImageView(ImageView const&) = delete;
  ImageView(ImageView&& other) noexcept;
  ImageView& operator=(ImageView const&) = delete;
  ImageView& operator=(ImageView&& other) noexcept;
  ~ImageView() noexcept;

private:
  std::string name;
}; // struct ImageView

struct Image {
  static tl::expected<Image, std::system_error>
  Create(VkImageType type, VkFormat format, VkExtent3D extent,
         std::uint32_t mipLevels, std::uint32_t arrayLayers,
         VkSampleCountFlagBits samples, VkImageUsageFlags usage,
         VmaMemoryUsage memoryUsage, std::string name = {}) noexcept;

  static tl::expected<Image, std::system_error>
  CreateFromMemory(VkImageType imageType, VkFormat format, VkExtent3D extent,
                   VkImageUsageFlags usage, VmaMemoryUsage memoryUsage,
                   gsl::not_null<unsigned char*> pixels,
                   std::uint32_t bytes_per_pixel,
                   std::string name = {}) noexcept;

  tl::expected<ImageView, std::system_error> CreateImageView(
    VkImageViewType type_, VkImageSubresourceRange imageSubresourceRange,
    std::string name_ = {},
    VkComponentMapping componentMapping = {
      VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
      VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY}) noexcept {
    return ImageView::Create(handle, format, type_, imageSubresourceRange,
                             name_, componentMapping);
  }

  tl::expected<void, std::system_error>
  Transition(VkImageLayout oldLayout, VkImageLayout newLayout,
             std::uint32_t mipLevels = 1,
             std::uint32_t arrayLayers = 1) noexcept;

  VkImageType type;
  VkFormat format{VK_FORMAT_UNDEFINED};
  VkImage handle{VK_NULL_HANDLE};
  VmaAllocation allocation{VK_NULL_HANDLE};

  operator VkImage() const noexcept { return handle; }
  VkImage* get() noexcept { return &handle; }

  Image() = default;
  Image(Image const&) = delete;
  Image(Image&& other) noexcept;
  Image& operator=(Image const&) = delete;
  Image& operator=(Image&& other) noexcept;
  ~Image() noexcept;

private:
  std::string name;
}; // struct Image

tl::expected<VkImageView, std::system_error> CreateImageView(
  VkImage image, VkFormat format, VkImageViewType type,
  VkImageSubresourceRange imageSubresourceRange,
  VkComponentMapping componentMapping = {
    VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
    VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY}) noexcept;

} // namespace iris::Renderer

#endif // HEV_IRIS_RENDERER_IMAGE_H_
