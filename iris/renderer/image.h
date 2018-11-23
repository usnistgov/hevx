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
  ImageView& operator=(ImageView&& rhs) noexcept;
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
                   gsl::not_null<std::byte*> pixels,
                   std::uint32_t bytesPerPixel, std::string name = {},
                   VkCommandPool commandPool = VK_NULL_HANDLE) noexcept;

  tl::expected<ImageView, std::system_error> CreateImageView(
    VkImageViewType type_, VkImageSubresourceRange imageSubresourceRange,
    std::string name_ = {},
    VkComponentMapping componentMapping = {
      VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
      VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY}) noexcept {
    return ImageView::Create(handle, format, type_, imageSubresourceRange,
                             name_, componentMapping);
  }

  [[nodiscard]] std::system_error
  Transition(VkImageLayout oldLayout, VkImageLayout newLayout,
             std::uint32_t mipLevels = 1, std::uint32_t arrayLayers = 1,
             VkCommandPool commandPool = VK_NULL_HANDLE) noexcept;

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
  Image& operator=(Image&& rhs) noexcept;
  ~Image() noexcept;

private:
  std::string name;
}; // struct Image

struct Sampler {
  static tl::expected<Sampler, std::system_error>
  Create(VkSamplerCreateInfo const& samplerCI, std::string name = {}) noexcept;

  VkSampler handle{VK_NULL_HANDLE};
  VkSampler* get() noexcept { return &handle; }

  operator VkSampler() const noexcept { return handle; }

  Sampler() = default;
  Sampler(Sampler const&) = delete;
  Sampler(Sampler&& other) noexcept;
  Sampler& operator=(Sampler const&) = delete;
  Sampler& operator=(Sampler&& rhs) noexcept;
  ~Sampler() noexcept;

private:
  std::string name;
}; // struct Sampler

} // namespace iris::Renderer

#endif // HEV_IRIS_RENDERER_IMAGE_H_
