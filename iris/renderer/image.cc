#include "renderer/image.h"
#include "renderer/buffer.h"
#include "renderer/impl.h"
#include "error.h"
#include "logging.h"

tl::expected<VkImageView, std::error_code>
iris::Renderer::CreateImageView(VkImage image, VkFormat format,
                                VkImageViewType viewType,
                                VkImageSubresourceRange imageSubresourceRange,
                                VkComponentMapping componentMapping) noexcept {
  IRIS_LOG_ENTER();
  VkResult result;

  VkImageViewCreateInfo ci = {};
  ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  ci.image = image;
  ci.viewType = viewType;
  ci.format = format;
  ci.components = componentMapping;
  ci.subresourceRange = imageSubresourceRange;

  VkImageView imageView;
  result = vkCreateImageView(sDevice, &ci, nullptr, &imageView);
  if (result != VK_SUCCESS) {
    GetLogger()->error("Cannot create image view: {}", to_string(result));
    IRIS_LOG_LEAVE();
    return tl::unexpected(make_error_code(result));
  }

  IRIS_LOG_LEAVE();
  return imageView;
} // iris::Renderer::CreateImageAndView

tl::expected<std::tuple<VkImage, VmaAllocation, VkImageView>, std::error_code>
iris::Renderer::CreateImageAndView(
  VkImageType imageType, VkFormat format, VkExtent3D extent,
  std::uint32_t mipLevels, std::uint32_t arrayLayers,
  VkSampleCountFlagBits samples, VkImageUsageFlags usage,
  VmaMemoryUsage memoryUsage, VkImageViewType viewType,
  VkImageSubresourceRange imageSubresourceRange,
  VkComponentMapping componentMapping) noexcept {
  IRIS_LOG_ENTER();
  VkResult result;

  VkImageCreateInfo ici = {};
  ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  ici.imageType = imageType;
  ici.format = format;
  ici.extent = extent;
  ici.mipLevels = mipLevels;
  ici.arrayLayers = arrayLayers;
  ici.samples = samples;
  ici.tiling = VK_IMAGE_TILING_OPTIMAL;
  ici.usage = usage;
  ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

  VmaAllocationCreateInfo aci = {};
  aci.usage = memoryUsage;

  VkImage image;
  VmaAllocation allocation;

  result = vmaCreateImage(sAllocator, &ici, &aci, &image, &allocation, nullptr);
  if (result != VK_SUCCESS) {
    GetLogger()->error("Error creating or allocating image: {}",
                        to_string(result));
    IRIS_LOG_LEAVE();
    return tl::unexpected(make_error_code(result));
  }

  VkImageView view;
  if (auto v = CreateImageView(image, format, viewType, imageSubresourceRange,
                               componentMapping)) {
    view = *v;
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(v.error());
  }

  IRIS_LOG_LEAVE();
  return std::make_tuple(image, allocation, view);
} // iris::Renderer::CreateImageAndView

tl::expected<std::pair<VkImage, VmaAllocation>, std::error_code>
iris::Renderer::CreateImageFromMemory(VkImageType imageType, VkFormat format,
                                      VkExtent3D extent,
                                      VkImageUsageFlags usage,
                                      VmaMemoryUsage memoryUsage,
                                      unsigned char* pixels,
                                      std::uint32_t bytes_per_pixel) noexcept {
  IRIS_LOG_ENTER();

  VkResult result;
  VkDeviceSize imageSize;

  switch(format) {
  case VK_FORMAT_R8G8B8A8_UNORM:
    if (bytes_per_pixel != sizeof(char) * 4) {
      GetLogger()->error("Invalid bytes_per_pixel: {}, expecting {}",
                         bytes_per_pixel, sizeof(char) * 4);
      return tl::unexpected(std::make_error_code(std::errc::invalid_argument));
    }

    imageSize = extent.width * extent.height * extent.depth * sizeof(char) * 4;
    break;

  case VK_FORMAT_R32_SFLOAT:
    if (bytes_per_pixel != sizeof(float)) {
      GetLogger()->error("Invalid bytes_per_pixel: {}, expecting {}",
                         bytes_per_pixel, sizeof(float));
      return tl::unexpected(std::make_error_code(std::errc::invalid_argument));
    }

    imageSize = extent.width * extent.height * extent.depth * sizeof(float);
    break;

  default:
      GetLogger()->error("Unsupported texture format");
      return tl::unexpected(std::make_error_code(std::errc::invalid_argument));
  }

  VkBuffer stagingBuffer;
  VmaAllocation stagingBufferAllocation;

  if (auto sb = CreateBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                             VMA_MEMORY_USAGE_CPU_TO_GPU)) {
    std::tie(stagingBuffer, stagingBufferAllocation) = *sb;
  } else {
    GetLogger()->error("Cannot allocate staging buffer: {}",
                       sb.error().message());
    return tl::unexpected(sb.error());
  }

  void* pBuffer;
  result = vmaMapMemory(sAllocator, stagingBufferAllocation, &pBuffer);
  if (result != VK_SUCCESS) {
    GetLogger()->error("Cannot map staging buffer: {}", to_string(result));
    return tl::unexpected(make_error_code(result));
  }

  std::memcpy(pBuffer, pixels, imageSize);
  vmaFlushAllocation(sAllocator, stagingBufferAllocation, 0, VK_WHOLE_SIZE);
  vmaUnmapMemory(sAllocator, stagingBufferAllocation);

  VkImageCreateInfo imageCI = {};
  imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageCI.imageType = imageType;
  imageCI.format = format;
  imageCI.extent = extent;
  imageCI.mipLevels = 1;
  imageCI.arrayLayers = 1;
  imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
  imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageCI.usage = usage | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  imageCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  imageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

  VmaAllocationCreateInfo allocationCI = {};
  allocationCI.usage = memoryUsage;

  VkImage image;
  VmaAllocation allocation;
  result = vmaCreateImage(sAllocator, &imageCI, &allocationCI, &image,
                          &allocation, nullptr);
  if (result != VK_SUCCESS) {
    GetLogger()->error("Cannot create image: {}", to_string(result));
    return tl::unexpected(make_error_code(result));
  }

  if (auto error = TransitionImage(image, VK_IMAGE_LAYOUT_UNDEFINED,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)) {
    return tl::unexpected(error);
  }

  VkCommandBuffer commandBuffer;
  if (auto cb = BeginOneTimeSubmit()) {
    commandBuffer = *cb;
  } else {
    return tl::unexpected(cb.error());
  }

  VkBufferImageCopy region = {};
  region.bufferOffset = 0;
  region.bufferRowLength = 0;
  region.bufferImageHeight = 0;
  region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  region.imageOffset = {0, 0};
  region.imageExtent = extent;

  vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

  if (auto error = EndOneTimeSubmit(commandBuffer)) {
    GetLogger()->error("Cannot copy staging buffer to image: {}",
                       error.message());
    return tl::unexpected(error);
  }

  if (auto error = TransitionImage(image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                   (memoryUsage == VMA_MEMORY_USAGE_GPU_ONLY
                                      ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                      : VK_IMAGE_LAYOUT_GENERAL))) {
    return tl::unexpected(error);
  }

  vmaDestroyBuffer(sAllocator, stagingBuffer, stagingBufferAllocation);

  IRIS_LOG_LEAVE();
  return std::make_pair(image, allocation);
} // iris::Renderer::CreateImageFromMemory

std::error_code
iris::Renderer::TransitionImage(VkImage image, VkImageLayout oldLayout,
                                VkImageLayout newLayout,
                                std::uint32_t mipLevels) noexcept {
  IRIS_LOG_ENTER();

  VkImageMemoryBarrier barrier = {};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.oldLayout = oldLayout;
  barrier.newLayout = newLayout;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image;
  barrier.subresourceRange.baseMipLevel = 0;
  barrier.subresourceRange.levelCount = mipLevels;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = 1;

  if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    // FIXME: handle stencil
  } else {
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  }

  VkPipelineStageFlagBits srcStage;
  VkPipelineStageFlagBits dstStage;

  if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
      newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
             newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    dstStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
             newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
                            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  } else {
    GetLogger()->critical("Logic error: unsupported layout transition");
    std::terminate();
  }

  VkCommandBuffer commandBuffer;
  if (auto cb = BeginOneTimeSubmit()) {
    commandBuffer = *cb;
  } else {
    return cb.error();
  }

  vkCmdPipelineBarrier(commandBuffer, srcStage, dstStage, 0, 0, nullptr, 0,
                       nullptr, 1, &barrier);

  if (auto error = EndOneTimeSubmit(commandBuffer)) {
    return error;
  }

  IRIS_LOG_LEAVE();
  return VulkanResult::kSuccess;
} // iris::Renderer::TransitionImage

