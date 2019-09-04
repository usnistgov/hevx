#include "image.h"
#include "config.h"

#include "buffer.h"
#include "error.h"
#include "logging.h"
#include "renderer.h"
#include "renderer_private.h"

iris::expected<void, std::system_error>
iris::TransitionImage(VkCommandPool commandPool, VkQueue queue, VkFence fence,
                      Image image, VkImageLayout oldLayout,
                      VkImageLayout newLayout, std::uint32_t mipLevels,
                      std::uint32_t arrayLayers) noexcept {
  IRIS_LOG_ENTER();
  Expects(commandPool != VK_NULL_HANDLE);
  Expects(queue != VK_NULL_HANDLE);
  Expects(fence != VK_NULL_HANDLE);
  Expects(image.image != VK_NULL_HANDLE);

  VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
    aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    // TODO: handle stencil
  }

  VkPipelineStageFlagBits srcStage;
  VkPipelineStageFlagBits dstStage;

  if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
      newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
    srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
             newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
             newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
    srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    dstStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
             newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
    srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  } else {
    IRIS_LOG_LEAVE();
    return unexpected(
      std::system_error(Error::kImageTransitionFailed, "Not implemented"));
  }

  VkCommandBuffer commandBuffer;
  if (auto cb = Renderer::BeginOneTimeSubmit(commandPool)) {
    commandBuffer = *cb;
  } else {
    IRIS_LOG_LEAVE();
    return unexpected(cb.error());
  }

  SetImageLayout(commandBuffer, image, srcStage, dstStage, oldLayout, newLayout,
                 aspectMask, mipLevels, arrayLayers);

  if (auto result =
        Renderer::EndOneTimeSubmit(commandBuffer, commandPool, queue, fence);
      !result) {
    IRIS_LOG_LEAVE();
    return unexpected(result.error());
  }

  IRIS_LOG_LEAVE();
  return {};
} // iris::TransitionImage

iris::expected<iris::Image, std::system_error>
iris::AllocateImage(VkFormat format, VkExtent2D extent, std::uint32_t mipLevels,
                    std::uint32_t arrayLayers,
                    VkSampleCountFlagBits sampleCount,
                    VkImageUsageFlags imageUsage, VkImageTiling imageTiling,
                    VmaMemoryUsage memoryUsage) noexcept {
  IRIS_LOG_ENTER();
  Expects(Renderer::sDevice != VK_NULL_HANDLE);
  Expects(Renderer::sAllocator != VK_NULL_HANDLE);

  VkImageCreateInfo imageCI = {};
  imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageCI.imageType = VK_IMAGE_TYPE_2D;
  imageCI.format = format;
  imageCI.extent = {extent.width, extent.height, 1};
  imageCI.mipLevels = mipLevels;
  imageCI.arrayLayers = arrayLayers;
  imageCI.samples = sampleCount;
  imageCI.tiling = imageTiling;
  imageCI.usage = imageUsage;
  imageCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  imageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

  VmaAllocationCreateInfo allocationCI = {};
  allocationCI.usage = memoryUsage;

  Image image;

  if (auto result =
        vmaCreateImage(Renderer::sAllocator, &imageCI, &allocationCI,
                       &image.image, &image.allocation, nullptr);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    unexpected(
      std::system_error(make_error_code(result), "Cannot create image"));
  }

  Ensures(image.image != VK_NULL_HANDLE);
  Ensures(image.allocation != VK_NULL_HANDLE);

  IRIS_LOG_LEAVE();
  return image;
} // iris::AllocateImage

iris::expected<VkImageView, std::system_error>
iris::CreateImageView(Image image, VkImageViewType type, VkFormat format,
                      VkImageSubresourceRange subresourceRange) noexcept {
  IRIS_LOG_ENTER();
  Expects(Renderer::sDevice != VK_NULL_HANDLE);
  Expects(image.image != VK_NULL_HANDLE);
  Expects(image.allocation != VK_NULL_HANDLE);

  VkImageView imageView = VK_NULL_HANDLE;

  VkImageViewCreateInfo imageViewCI = {};
  imageViewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  imageViewCI.image = image.image;
  imageViewCI.viewType = type;
  imageViewCI.format = format;
  imageViewCI.components = {
    VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
    VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
  imageViewCI.subresourceRange = subresourceRange;

  if (auto result =
        vkCreateImageView(Renderer::sDevice, &imageViewCI, nullptr, &imageView);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    unexpected(
      std::system_error(make_error_code(result), "Cannot create image view"));
  }

  Ensures(imageView != VK_NULL_HANDLE);

  IRIS_LOG_LEAVE();
  return imageView;
} // iris::CreateImageView

iris::expected<iris::Image, std::system_error> iris::CreateImage(
  VkCommandPool commandPool, VkQueue queue, VkFence fence, VkFormat format,
  VkExtent2D extent, VkImageUsageFlags imageUsage, VmaMemoryUsage memoryUsage,
  gsl::not_null<std::byte*> pixels, std::uint32_t bytesPerPixel) noexcept {
  IRIS_LOG_ENTER();
  Expects(Renderer::sDevice != VK_NULL_HANDLE);
  Expects(Renderer::sAllocator != VK_NULL_HANDLE);
  Expects(commandPool != VK_NULL_HANDLE);
  Expects(queue != VK_NULL_HANDLE);
  Expects(fence != VK_NULL_HANDLE);

  VkDeviceSize imageSize = 0;

  switch (format) {
  case VK_FORMAT_R8G8B8A8_UNORM:
    Expects(bytesPerPixel == sizeof(char) * 4);
    imageSize = extent.width * extent.height * bytesPerPixel;
    break;

  case VK_FORMAT_R32_SFLOAT:
    Expects(bytesPerPixel == sizeof(float));
    imageSize = extent.width * extent.height * bytesPerPixel;
    break;

  default:
    IRIS_LOG_LEAVE();
    return unexpected(
      std::system_error(std::make_error_code(std::errc::invalid_argument),
                        "Unsupported texture format"));
  }

  auto staging = AllocateBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                VMA_MEMORY_USAGE_CPU_TO_GPU);
  if (!staging) {
    using namespace std::string_literals;
    IRIS_LOG_LEAVE();
    return unexpected(std::system_error(staging.error().code(),
                                        "Cannot create staging buffer: "s +
                                          staging.error().what()));
  }

  if (auto ptr = staging->Map<std::byte*>()) {
    std::memcpy(*ptr, pixels, imageSize);
    staging->Unmap();
  } else {
    using namespace std::string_literals;
    IRIS_LOG_LEAVE();
    return unexpected(std::system_error(
      ptr.error().code(), "Cannot map staging buffer: "s + ptr.error().what()));
  }

  VkImageCreateInfo imageCI = {};
  imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageCI.imageType = VK_IMAGE_TYPE_2D;
  imageCI.format = format;
  imageCI.extent = {extent.width, extent.height, 1};
  imageCI.mipLevels = 1;
  imageCI.arrayLayers = 1;
  imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
  imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageCI.usage = imageUsage | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  imageCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  imageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

  VmaAllocationCreateInfo allocationCI = {};
  allocationCI.usage = memoryUsage;

  Image image;

  if (auto result =
        vmaCreateImage(Renderer::sAllocator, &imageCI, &allocationCI,
                       &image.image, &image.allocation, nullptr);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return unexpected(
      std::system_error(make_error_code(result), "Cannot create image"));
  }

  if (auto result = TransitionImage(commandPool, queue, fence, image,
                                    VK_IMAGE_LAYOUT_UNDEFINED,
                                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, 1);
      !result) {
    return unexpected(result.error());
  }

  VkCommandBuffer commandBuffer;
  if (auto cb = Renderer::BeginOneTimeSubmit(commandPool)) {
    commandBuffer = *cb;
  } else {
    IRIS_LOG_LEAVE();
    return unexpected(cb.error());
  }

  VkBufferImageCopy region = {};
  region.bufferOffset = 0;
  region.bufferRowLength = 0;
  region.bufferImageHeight = 0;
  region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  region.imageOffset = {0, 0, 0};
  region.imageExtent = {extent.width, extent.height, 1};

  vkCmdCopyBufferToImage(commandBuffer, staging->buffer, image.image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

  if (auto result =
        Renderer::EndOneTimeSubmit(commandBuffer, commandPool, queue, fence);
      !result) {
    IRIS_LOG_LEAVE();
    return unexpected(result.error());
  }

  if (auto result = TransitionImage(
        commandPool, queue, fence, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        (memoryUsage == VMA_MEMORY_USAGE_GPU_ONLY
           ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
           : VK_IMAGE_LAYOUT_GENERAL),
        1, 1);
      !result) {
    return unexpected(result.error());
  }

  DestroyBuffer(*staging);

  Ensures(image.image != VK_NULL_HANDLE);
  Ensures(image.allocation != VK_NULL_HANDLE);

  IRIS_LOG_LEAVE();
  return image;
} // iris::CreateImage

iris::expected<iris::Image, std::system_error>
iris::CreateImage(VkCommandPool commandPool, VkQueue queue, VkFence fence,
                  VkFormat format, gsl::span<VkExtent2D> extents,
                  VkImageUsageFlags imageUsage, VmaMemoryUsage memoryUsage,
                  gsl::not_null<std::byte*> levelsPixels,
                  std::uint32_t bytesPerPixel) noexcept {
  IRIS_LOG_ENTER();
  Expects(Renderer::sDevice != VK_NULL_HANDLE);
  Expects(Renderer::sAllocator != VK_NULL_HANDLE);
  Expects(commandPool != VK_NULL_HANDLE);
  Expects(queue != VK_NULL_HANDLE);
  Expects(fence != VK_NULL_HANDLE);

  VkDeviceSize imageSize = 0;

  switch (format) {
  case VK_FORMAT_R8G8B8A8_UNORM:
    Expects(bytesPerPixel == sizeof(char) * 4);
    for (auto&& extent : extents) {
      imageSize += extent.width * extent.height * bytesPerPixel;
    }
    break;

  case VK_FORMAT_R32_SFLOAT:
    Expects(bytesPerPixel == sizeof(float));
    for (auto&& extent : extents) {
      imageSize += extent.width * extent.height * bytesPerPixel;
    }
    break;

  default:
    IRIS_LOG_LEAVE();
    return unexpected(
      std::system_error(std::make_error_code(std::errc::invalid_argument),
                        "Unsupported texture format"));
  }

  auto staging = AllocateBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                VMA_MEMORY_USAGE_CPU_TO_GPU);
  if (!staging) {
    using namespace std::string_literals;
    IRIS_LOG_LEAVE();
    return unexpected(std::system_error(staging.error().code(),
                                        "Cannot create staging buffer: "s +
                                          staging.error().what()));
  }

  if (auto ptr = staging->Map<std::byte*>()) {
    std::memcpy(*ptr, levelsPixels, imageSize);
    staging->Unmap();
  } else {
    using namespace std::string_literals;
    IRIS_LOG_LEAVE();
    return unexpected(std::system_error(
      ptr.error().code(), "Cannot map staging buffer: "s + ptr.error().what()));
  }

  VkImageCreateInfo imageCI = {};
  imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageCI.imageType = VK_IMAGE_TYPE_2D;
  imageCI.format = format;
  imageCI.extent = {extents[0].width, extents[0].height, 1};
  imageCI.mipLevels = gsl::narrow_cast<std::uint32_t>(extents.size());
  imageCI.arrayLayers = 1;
  imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
  imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageCI.usage = imageUsage | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  imageCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  imageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

  VmaAllocationCreateInfo allocationCI = {};
  allocationCI.usage = memoryUsage;

  Image image;

  if (auto result =
        vmaCreateImage(Renderer::sAllocator, &imageCI, &allocationCI,
                       &image.image, &image.allocation, nullptr);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return unexpected(
      std::system_error(make_error_code(result), "Cannot create image"));
  }

  if (auto result = TransitionImage(
        commandPool, queue, fence, image, VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, imageCI.mipLevels, 1);
      !result) {
    return unexpected(result.error());
  }

  VkCommandBuffer commandBuffer;
  if (auto cb = Renderer::BeginOneTimeSubmit(commandPool)) {
    commandBuffer = *cb;
  } else {
    IRIS_LOG_LEAVE();
    return unexpected(cb.error());
  }

  VkBufferImageCopy region = {};
  region.bufferOffset = 0;
  region.bufferRowLength = 0;
  region.bufferImageHeight = 0;
  region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  region.imageOffset = {0, 0, 0};
  region.imageExtent = {extents[0].width, extents[0].height, 1};

  IRIS_LOG_DEBUG("Copying offset {} to level {} ({}x{})", region.bufferOffset,
                 region.imageSubresource.mipLevel, region.imageExtent.width,
                 region.imageExtent.height);
  vkCmdCopyBufferToImage(commandBuffer, staging->buffer, image.image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

  for (std::size_t i = 1; i < static_cast<std::size_t>(extents.size()); ++i) {
    region.bufferOffset +=
      region.imageExtent.width * region.imageExtent.height * bytesPerPixel;
    region.imageSubresource.mipLevel = gsl::narrow_cast<std::uint32_t>(i);
    region.imageExtent = {extents[i].width, extents[i].height, 1};

    IRIS_LOG_DEBUG("Copying offset {} to level {} ({}x{})", region.bufferOffset,
                   region.imageSubresource.mipLevel, region.imageExtent.width,
                   region.imageExtent.height);
    vkCmdCopyBufferToImage(commandBuffer, staging->buffer, image.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
  }

  if (auto result =
        Renderer::EndOneTimeSubmit(commandBuffer, commandPool, queue, fence);
      !result) {
    IRIS_LOG_LEAVE();
    return unexpected(result.error());
  }

  if (auto result = TransitionImage(
        commandPool, queue, fence, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        (memoryUsage == VMA_MEMORY_USAGE_GPU_ONLY
           ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
           : VK_IMAGE_LAYOUT_GENERAL),
        imageCI.mipLevels, 1);
      !result) {
    return unexpected(result.error());
  }

  DestroyBuffer(*staging);

  Ensures(image.image != VK_NULL_HANDLE);
  Ensures(image.allocation != VK_NULL_HANDLE);

  IRIS_LOG_LEAVE();
  return image;
} // iris::CreateImage

void iris::DestroyImage(Image image) noexcept {
  vmaDestroyImage(Renderer::sAllocator, image.image, image.allocation);
} // iris::DestroyImage
