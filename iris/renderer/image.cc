#include "renderer/image.h"
#include "logging.h"
#include "renderer/buffer.h"

tl::expected<iris::Renderer::ImageView, std::system_error>
iris::Renderer::ImageView::Create(
  VkImage image, VkFormat format, VkImageViewType type,
  VkImageSubresourceRange imageSubresourceRange, std::string name,
  VkComponentMapping componentMapping) noexcept {
  IRIS_LOG_ENTER();
  Expects(sDevice != VK_NULL_HANDLE);
  Expects(image != VK_NULL_HANDLE);

  ImageView view;

  VkImageViewCreateInfo ci = {};
  ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  ci.image = image;
  ci.viewType = type;
  ci.format = format;
  ci.components = componentMapping;
  ci.subresourceRange = imageSubresourceRange;

  if (auto result = vkCreateImageView(sDevice, &ci, nullptr, &view.handle);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(make_error_code(result), "Cannot create image view"));
  }

  if (!name.empty()) {
    NameObject(VK_OBJECT_TYPE_IMAGE_VIEW, view.handle, name.c_str());
  }

  view.name = std::move(name);

  Ensures(view.handle != VK_NULL_HANDLE);
  IRIS_LOG_LEAVE();
  return std::move(view);
} // iris::Renderer::ImageView::Create

iris::Renderer::ImageView::ImageView(ImageView&& other) noexcept
  : type(other.type)
  , format(other.format)
  , handle(other.handle)
  , name(std::move(other.name)) {
  other.handle = VK_NULL_HANDLE;
} // iris::Renderer::ImageView::ImageView

iris::Renderer::ImageView& iris::Renderer::ImageView::operator=(ImageView&& rhs) noexcept {
  if (this == &rhs) return *this;

  type = rhs.type;
  format = rhs.format;
  handle = rhs.handle;
  name = std::move(rhs.name);

  rhs.handle = VK_NULL_HANDLE;

  return *this;
} // iris::Renderer::ImageView::operator=

iris::Renderer::ImageView::~ImageView() noexcept {
  if (handle == VK_NULL_HANDLE) return;
  IRIS_LOG_ENTER();

  vkDestroyImageView(sDevice, handle, nullptr);

  IRIS_LOG_LEAVE();
} // iris::Renderer::ImageView::~ImageView

tl::expected<iris::Renderer::Image, std::system_error>
iris::Renderer::Image::Create(VkImageType type, VkFormat format,
                              VkExtent3D extent, std::uint32_t mipLevels,
                              std::uint32_t arrayLayers,
                              VkSampleCountFlagBits samples,
                              VkImageUsageFlags usage,
                              VmaMemoryUsage memoryUsage,
                              std::string name) noexcept {
  IRIS_LOG_ENTER();
  Expects(sDevice != VK_NULL_HANDLE);

  Image image;

  VkImageCreateInfo ici = {};
  ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  ici.imageType = type;
  ici.format = format;
  ici.extent = extent;
  ici.mipLevels = mipLevels;
  ici.arrayLayers = arrayLayers;
  ici.samples = samples;
  ici.tiling = VK_IMAGE_TILING_OPTIMAL;
  ici.usage = usage;
  ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

  VmaAllocationCreateInfo allocationCI = {};
  allocationCI.usage = memoryUsage;

  if (!name.empty()) {
    allocationCI.flags = VMA_ALLOCATION_CREATE_USER_DATA_COPY_STRING_BIT;
    allocationCI.pUserData = name.data();
  }

  if (auto result = vmaCreateImage(sAllocator, &ici, &allocationCI,
                                   &image.handle, &image.allocation, nullptr);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(make_error_code(result),
                                            "Cannot create or allocate image"));
  }

  if (!name.empty()) {
    NameObject(VK_OBJECT_TYPE_IMAGE, image.handle, name.c_str());
  }

  image.type = type;
  image.format = format;
  image.name = std::move(name);

  Ensures(image.handle != VK_NULL_HANDLE);
  IRIS_LOG_LEAVE();
  return std::move(image);
} // iris::Renderer::Image::Create

tl::expected<iris::Renderer::Image, std::system_error>
iris::Renderer::Image::CreateFromMemory(
  VkImageType type, VkFormat format, VkExtent3D extent, VkImageUsageFlags usage,
  VmaMemoryUsage memoryUsage, gsl::not_null<unsigned char*> pixels,
  std::uint32_t bytes_per_pixel, std::string name) noexcept {
  IRIS_LOG_ENTER();
  Expects(sDevice != VK_NULL_HANDLE);

  Image image;
  VkDeviceSize imageSize;

  switch(format) {
  case VK_FORMAT_R8G8B8A8_UNORM:
    Expects(bytes_per_pixel == sizeof(char) * 4);
    imageSize = extent.width * extent.height * extent.depth * sizeof(char) * 4;
    break;

  case VK_FORMAT_R32_SFLOAT:
    Expects(bytes_per_pixel == sizeof(float));
    imageSize = extent.width * extent.height * extent.depth * sizeof(float);
    break;

  default:
      GetLogger()->error("Unsupported texture format");
      std::terminate();
  }

  Buffer stagingBuffer;
  if (auto sb = Buffer::Create(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                               VMA_MEMORY_USAGE_CPU_TO_GPU)) {
    stagingBuffer = std::move(*sb);
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(sb.error().code(), "Cannot create staging buffer"));
  }

  if (auto p = stagingBuffer.Map<unsigned char*>()) {
    std::memcpy(*p, pixels, imageSize);
  } else {
    using namespace std::string_literals;
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(
      p.error().code(), "Cannot map staging buffer: "s + p.error().what()));
  }

  stagingBuffer.Unmap();

  VkImageCreateInfo imageCI = {};
  imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageCI.imageType = type;
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

  if (!name.empty()) {
    allocationCI.flags = VMA_ALLOCATION_CREATE_USER_DATA_COPY_STRING_BIT;
    allocationCI.pUserData = name.data();
  }

  if (auto result = vmaCreateImage(sAllocator, &imageCI, &allocationCI,
                                   &image.handle, &image.allocation, nullptr);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(make_error_code(result), "Cannot create image"));
  }

  if (auto error = image.Transition(VK_IMAGE_LAYOUT_UNDEFINED,
                                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
      error.code()) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(error);
  }

  VkCommandBuffer commandBuffer;
  if (auto cb = BeginOneTimeSubmit()) {
    commandBuffer = *cb;
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(cb.error());
  }

  VkBufferImageCopy region = {};
  region.bufferOffset = 0;
  region.bufferRowLength = 0;
  region.bufferImageHeight = 0;
  region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  region.imageOffset = {0, 0, 0};
  region.imageExtent = extent;

  vkCmdCopyBufferToImage(commandBuffer, stagingBuffer.handle, image.handle,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

  if (auto error = EndOneTimeSubmit(commandBuffer); error.code()) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(error);
  }

  if (auto error =
        image.Transition(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         (memoryUsage == VMA_MEMORY_USAGE_GPU_ONLY
                            ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                            : VK_IMAGE_LAYOUT_GENERAL));
      error.code()) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(error);
  }

  if (!name.empty()) {
    NameObject(VK_OBJECT_TYPE_IMAGE, image.handle, name.c_str());
  }

  image.type = type;
  image.format = format;
  image.name = std::move(name);

  Ensures(image.handle != VK_NULL_HANDLE);
  IRIS_LOG_LEAVE();
  return std::move(image);
} // iris::Renderer::Image::CreateFromMemory

std::system_error iris::Renderer::Image::Transition(
  VkImageLayout oldLayout, VkImageLayout newLayout, std::uint32_t mipLevels,
  std::uint32_t arrayLayers) noexcept {
  IRIS_LOG_ENTER();
  Expects(sDevice != VK_NULL_HANDLE);
  Expects(handle != VK_NULL_HANDLE);

  VkImageMemoryBarrier barrier = {};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.oldLayout = oldLayout;
  barrier.newLayout = newLayout;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = handle;
  barrier.subresourceRange.baseMipLevel = 0;
  barrier.subresourceRange.levelCount = mipLevels;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = arrayLayers;

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
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
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
    IRIS_LOG_LEAVE();
    return cb.error();
  }

  vkCmdPipelineBarrier(commandBuffer, srcStage, dstStage, 0, 0, nullptr, 0,
                       nullptr, 1, &barrier);

  if (auto error = EndOneTimeSubmit(commandBuffer); error.code()) {
    IRIS_LOG_LEAVE();
    return error;
  }

  IRIS_LOG_LEAVE();
  return {Error::kNone};
} // iris::Renderer::Image::Transition

iris::Renderer::Image::Image(Image&& other) noexcept
  : type(other.type)
  , format(other.format)
  , handle(other.handle)
  , allocation(other.allocation)
  , name(std::move(other.name)) {
  other.handle = VK_NULL_HANDLE;
  other.allocation = VK_NULL_HANDLE;
} // iris::Renderer::Image::Image

iris::Renderer::Image& iris::Renderer::Image::operator=(Image&& rhs) noexcept {
  if (this == &rhs) return *this;

  type = rhs.type;
  format = rhs.format;
  handle = rhs.handle;
  allocation = rhs.allocation;
  name = std::move(rhs.name);

  rhs.handle = VK_NULL_HANDLE;
  rhs.allocation = VK_NULL_HANDLE;

  return *this;
} // iris::Renderer::Image::operator=

iris::Renderer::Image::~Image() noexcept {
  if (handle == VK_NULL_HANDLE) return;
  IRIS_LOG_ENTER();

  vmaDestroyImage(sAllocator, handle, allocation);

  IRIS_LOG_LEAVE();
} // iris::Renderer::Image::~Image

tl::expected<VkImageView, std::system_error>
iris::Renderer::CreateImageView(VkImage image, VkFormat format,
                                VkImageViewType type,
                                VkImageSubresourceRange imageSubresourceRange,
                                VkComponentMapping componentMapping) noexcept {
  IRIS_LOG_ENTER();
  VkResult result;

  VkImageViewCreateInfo ci = {};
  ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  ci.image = image;
  ci.viewType = type;
  ci.format = format;
  ci.components = componentMapping;
  ci.subresourceRange = imageSubresourceRange;

  VkImageView imageView;
  result = vkCreateImageView(sDevice, &ci, nullptr, &imageView);
  if (result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(make_error_code(result), "Cannot create image view"));
  }

  IRIS_LOG_LEAVE();
  return imageView;
} // iris::Renderer::CreateImageView

