#include "renderer_util.h"
#if PLATFORM_COMPILER_GCC
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#include "absl/container/inlined_vector.h"
#include "enumerate.h"
#include "error.h"
#include "io/read_file.h"
#include "logging.h"
#include "vulkan_util.h"

#if PLATFORM_COMPILER_GCC
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#endif
#include "SPIRV/GLSL.std.450.h"
#include "SPIRV/GlslangToSpv.h"
#include "glslang/Public/ShaderLang.h"
#if PLATFORM_COMPILER_GCC
#pragma GCC diagnostic pop
#endif

#include <fstream>

tl::expected<VkCommandBuffer, std::system_error>
iris::Renderer::BeginOneTimeSubmit(VkCommandPool commandPool) noexcept {
  IRIS_LOG_ENTER();
  Expects(sDevice != VK_NULL_HANDLE);
  Expects(commandPool != VK_NULL_HANDLE);

  VkCommandBufferAllocateInfo commandBufferAI = {};
  commandBufferAI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  commandBufferAI.commandPool = commandPool;
  commandBufferAI.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  commandBufferAI.commandBufferCount = 1;

  VkCommandBuffer commandBuffer;
  if (auto result =
        vkAllocateCommandBuffers(sDevice, &commandBufferAI, &commandBuffer);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(make_error_code(result),
                                            "Cannot allocate command buffer"));
  }

  VkCommandBufferBeginInfo commandBufferBI = {};
  commandBufferBI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  commandBufferBI.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

  if (auto result = vkBeginCommandBuffer(commandBuffer, &commandBufferBI);
      result != VK_SUCCESS) {
    vkFreeCommandBuffers(sDevice, commandPool, 1, &commandBuffer);
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(make_error_code(result),
                                            "Cannot begin command buffer"));
  }

  IRIS_LOG_LEAVE();
  return commandBuffer;
} // iris::Renderer::BeginOneTimeSubmit

tl::expected<void, std::system_error>
iris::Renderer::EndOneTimeSubmit(VkCommandBuffer commandBuffer,
                                 VkCommandPool commandPool, VkQueue queue,
                                 VkFence fence) noexcept {
  IRIS_LOG_ENTER();
  Expects(sDevice != VK_NULL_HANDLE);
  Expects(commandBuffer != VK_NULL_HANDLE);
  Expects(commandPool != VK_NULL_HANDLE);
  Expects(queue != VK_NULL_HANDLE);
  Expects(fence != VK_NULL_HANDLE);

  VkSubmitInfo submitI = {};
  submitI.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitI.commandBufferCount = 1;
  submitI.pCommandBuffers = &commandBuffer;

  if (auto result = vkEndCommandBuffer(commandBuffer); result != VK_SUCCESS) {
    vkFreeCommandBuffers(sDevice, commandPool, 1, &commandBuffer);
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(make_error_code(result), "Cannot end command buffer"));
  }

  if (auto result = vkQueueSubmit(queue, 1, &submitI, fence);
      result != VK_SUCCESS) {
    vkFreeCommandBuffers(sDevice, commandPool, 1, &commandBuffer);
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(make_error_code(result),
                                            "Cannot submit command buffer"));
  }

  if (auto result = vkWaitForFences(sDevice, 1, &fence, VK_TRUE, UINT64_MAX);
      result != VK_SUCCESS) {
    vkFreeCommandBuffers(sDevice, commandPool, 1, &commandBuffer);
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(
      make_error_code(result), "Cannot wait on one-time submit fence"));
  }

  if (auto result = vkResetFences(sDevice, 1, &fence); result != VK_SUCCESS) {
    vkFreeCommandBuffers(sDevice, commandPool, 1, &commandBuffer);
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(
      make_error_code(result), "Cannot reset one-time submit fence"));
  }

  vkFreeCommandBuffers(sDevice, commandPool, 1, &commandBuffer);
  IRIS_LOG_LEAVE();
  return {};
} // iris::Renderer::EndOneTimeSubmit

tl::expected<void, std::system_error> iris::Renderer::TransitionImage(
  VkCommandPool commandPool, VkQueue queue, VkFence fence, VkImage image,
  VkImageLayout oldLayout, VkImageLayout newLayout, std::uint32_t mipLevels,
  std::uint32_t arrayLayers) noexcept {
  IRIS_LOG_ENTER();
  Expects(sDevice != VK_NULL_HANDLE);
  Expects(commandPool != VK_NULL_HANDLE);
  Expects(queue != VK_NULL_HANDLE);
  Expects(fence != VK_NULL_HANDLE);
  Expects(image != VK_NULL_HANDLE);

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
  barrier.subresourceRange.layerCount = arrayLayers;

  if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    // TODO: handle stencil
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
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(Error::kImageTransitionFailed, "Not implemented"));
  }

  VkCommandBuffer commandBuffer;
  if (auto cb = BeginOneTimeSubmit(commandPool)) {
    commandBuffer = *cb;
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(cb.error());
  }

  vkCmdPipelineBarrier(commandBuffer, srcStage, dstStage, 0, 0, nullptr, 0,
                       nullptr, 1, &barrier);

  if (auto result = EndOneTimeSubmit(commandBuffer, commandPool, queue, fence);
      !result) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(result.error());
  }

  IRIS_LOG_LEAVE();
  return {};
} // iris::Renderer::TransitionImage

tl::expected<iris::Renderer::Image, std::system_error>
iris::Renderer::AllocateImage(VkFormat format, VkExtent2D extent,
                              std::uint32_t mipLevels,
                              std::uint32_t arrayLayers,
                              VkSampleCountFlagBits sampleCount,
                              VkImageUsageFlags imageUsage,
                              VkImageTiling imageTiling,
                              VmaMemoryUsage memoryUsage) noexcept {
  IRIS_LOG_ENTER();
  Expects(sDevice != VK_NULL_HANDLE);
  Expects(sAllocator != VK_NULL_HANDLE);

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

  if (auto result = vmaCreateImage(sAllocator, &imageCI, &allocationCI, &image.image,
                                   &image.allocation, nullptr);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    tl::unexpected(
      std::system_error(make_error_code(result), "Cannot create image"));
  }

  Ensures(image.image != VK_NULL_HANDLE);
  Ensures(image.allocation != VK_NULL_HANDLE);

  IRIS_LOG_LEAVE();
  return image;
} // iris::Renderer::AllocateImage

tl::expected<VkImageView, std::system_error> iris::Renderer::CreateImageView(
  Image image, VkImageViewType type, VkFormat format,
  VkImageSubresourceRange subresourceRange) noexcept {
  IRIS_LOG_ENTER();
  Expects(sDevice != VK_NULL_HANDLE);
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
        vkCreateImageView(sDevice, &imageViewCI, nullptr, &imageView);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    tl::unexpected(
      std::system_error(make_error_code(result), "Cannot create image view"));
  }

  Ensures(imageView != VK_NULL_HANDLE);

  IRIS_LOG_LEAVE();
  return imageView;
} // iris::Renderer::CreateImageView

tl::expected<iris::Renderer::Image, std::system_error>
iris::Renderer::CreateImage(VkCommandPool commandPool, VkQueue queue,
                            VkFence fence, VkFormat format, VkExtent2D extent,
                            VkImageUsageFlags imageUsage,
                            VmaMemoryUsage memoryUsage,
                            gsl::not_null<std::byte*> pixels,
                            std::uint32_t bytesPerPixel) noexcept {
  IRIS_LOG_ENTER();
  Expects(sDevice != VK_NULL_HANDLE);
  Expects(sAllocator != VK_NULL_HANDLE);
  Expects(commandPool != VK_NULL_HANDLE);
  Expects(queue != VK_NULL_HANDLE);
  Expects(fence != VK_NULL_HANDLE);

  VkDeviceSize imageSize [[maybe_unused]];

  switch (format) {
  case VK_FORMAT_R8G8B8A8_UNORM:
    Expects(bytesPerPixel == sizeof(char) * 4);
    imageSize = extent.width * extent.height * sizeof(char) * 4;
    break;

  case VK_FORMAT_R32_SFLOAT:
    Expects(bytesPerPixel == sizeof(float));
    imageSize = extent.width * extent.height * sizeof(float);
    break;

  default:
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(std::make_error_code(std::errc::invalid_argument),
                        "Unsupported texture format"));
  }

  VkBuffer stagingBuffer = VK_NULL_HANDLE;
  VmaAllocation stagingBufferAllocation = VK_NULL_HANDLE;
  VkDeviceSize stagingBufferSize = 0;

  if (auto bas = AllocateBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                VMA_MEMORY_USAGE_CPU_TO_GPU)) {
    std::tie(stagingBuffer, stagingBufferAllocation, stagingBufferSize) = *bas;
  } else {
    using namespace std::string_literals;
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(bas.error().code(), "Cannot create staging buffer: "s +
                                              bas.error().what()));
  }

  if (auto ptr = MapMemory<std::byte*>(stagingBufferAllocation)) {
    std::memcpy(*ptr, pixels, imageSize);
    UnmapMemory(stagingBufferAllocation);
  } else {
    using namespace std::string_literals;
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(
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

  if (auto result = vmaCreateImage(sAllocator, &imageCI, &allocationCI,
                                   &image.image, &image.allocation, nullptr);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    tl::unexpected(
      std::system_error(make_error_code(result), "Cannot create image"));
  }

  if (auto result = TransitionImage(commandPool, queue, fence, image.image,
                                    VK_IMAGE_LAYOUT_UNDEFINED,
                                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, 1);
      !result) {
    return tl::unexpected(result.error());
  }

  VkCommandBuffer commandBuffer;
  if (auto cb = BeginOneTimeSubmit(commandPool)) {
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
  region.imageExtent = {extent.width, extent.height, 1};

  vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, image.image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

  if (auto result = EndOneTimeSubmit(commandBuffer, commandPool, queue, fence);
      !result) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(result.error());
  }

  if (auto result =
        TransitionImage(commandPool, queue, fence, image.image,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        (memoryUsage == VMA_MEMORY_USAGE_GPU_ONLY
                           ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                           : VK_IMAGE_LAYOUT_GENERAL),
                        1, 1);
      !result) {
    return tl::unexpected(result.error());
  }

  DestroyBuffer(stagingBuffer, stagingBufferAllocation);

  Ensures(image.image != VK_NULL_HANDLE);
  Ensures(image.allocation != VK_NULL_HANDLE);

  IRIS_LOG_LEAVE();
  return image;
} // iris::Renderer::CreateImage

tl::expected<std::tuple<VkBuffer, VmaAllocation, VkDeviceSize>,
             std::system_error>
iris::Renderer::AllocateBuffer(VkDeviceSize size,
                               VkBufferUsageFlags bufferUsage,
                               VmaMemoryUsage memoryUsage) noexcept {
  IRIS_LOG_ENTER();
  Expects(sAllocator != VK_NULL_HANDLE);
  Expects(size > 0);

  VkBufferCreateInfo bufferCI = {};
  bufferCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferCI.size = size;
  bufferCI.usage = bufferUsage;

  VmaAllocationCreateInfo allocationCI = {};
  allocationCI.usage = memoryUsage;

  VkBuffer buffer = VK_NULL_HANDLE;
  VmaAllocation allocation = VK_NULL_HANDLE;

  if (auto result = vmaCreateBuffer(sAllocator, &bufferCI, &allocationCI,
                                    &buffer, &allocation, nullptr);
      result != VK_SUCCESS) {
    return tl::unexpected(
      std::system_error(make_error_code(result), "Cannot create buffer"));
  }

  Ensures(buffer != VK_NULL_HANDLE);
  Ensures(allocation != VK_NULL_HANDLE);

  IRIS_LOG_LEAVE();
  return std::make_tuple(buffer, allocation, size);
} // iris::Renderer::AllocateBuffer

tl::expected<std::tuple<VkBuffer, VmaAllocation, VkDeviceSize>,
             std::system_error>
iris::Renderer::ReallocateBuffer(VkBuffer buffer, VmaAllocation allocation,
                                 VkDeviceSize oldSize, VkDeviceSize newSize,
                                 VkBufferUsageFlags bufferUsage,
                                 VmaMemoryUsage memoryUsage) noexcept {
  IRIS_LOG_ENTER();
  Expects(sAllocator != VK_NULL_HANDLE);
  Expects(newSize > 0);

  if (buffer != VK_NULL_HANDLE && allocation != VK_NULL_HANDLE &&
      oldSize >= newSize) {
    IRIS_LOG_LEAVE();
    return std::make_tuple(buffer, allocation, oldSize);
  }

  VkBufferCreateInfo bufferCI = {};
  bufferCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferCI.size = newSize;
  bufferCI.usage = bufferUsage;

  VmaAllocationCreateInfo allocationCI = {};
  allocationCI.usage = memoryUsage;

  VkBuffer newBuffer;
  VmaAllocation newAllocation;

  if (auto result = vmaCreateBuffer(sAllocator, &bufferCI, &allocationCI,
                                    &newBuffer, &newAllocation, nullptr);
      result != VK_SUCCESS) {
    return tl::unexpected(
      std::system_error(make_error_code(result), "Cannot create buffer"));
  }

  if (buffer != VK_NULL_HANDLE && allocation != VK_NULL_HANDLE) {
    DestroyBuffer(buffer, allocation);
  }

  Ensures(newBuffer != VK_NULL_HANDLE);
  Ensures(newAllocation != VK_NULL_HANDLE);

  IRIS_LOG_LEAVE();
  return std::make_tuple(newBuffer, newAllocation, newSize);
} // iris::Renderer::ReallocateBuffer

tl::expected<std::tuple<VkBuffer, VmaAllocation, VkDeviceSize>,
             std::system_error>
iris::Renderer::CreateBuffer(VkCommandPool commandPool, VkQueue queue,
                             VkFence fence, VkBufferUsageFlags bufferUsage,
                             VmaMemoryUsage memoryUsage, VkDeviceSize size,
                             gsl::not_null<std::byte*> data) noexcept {
  IRIS_LOG_ENTER();
  Expects(sAllocator != VK_NULL_HANDLE);
  Expects(commandPool != VK_NULL_HANDLE);
  Expects(queue != VK_NULL_HANDLE);
  Expects(fence != VK_NULL_HANDLE);
  Expects(size > 0);

  VkBuffer stagingBuffer = VK_NULL_HANDLE;
  VmaAllocation stagingAllocation = VK_NULL_HANDLE;
  VkDeviceSize stagingSize = 0;

  if (auto bas = AllocateBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                VMA_MEMORY_USAGE_CPU_TO_GPU)) {
    std::tie(stagingBuffer, stagingAllocation, stagingSize) = *bas;
  } else {
    using namespace std::string_literals;
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(bas.error().code(), "Cannot create staging buffer: "s +
                                              bas.error().what()));
  }

  if (auto ptr = MapMemory<std::byte*>(stagingAllocation)) {
    std::memcpy(*ptr, data, size);
    UnmapMemory(stagingAllocation);
  } else {
    using namespace std::string_literals;
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(
      ptr.error().code(), "Cannot map staging buffer: "s + ptr.error().what()));
  }

  VkBufferCreateInfo bufferCI = {};
  bufferCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferCI.size = size;
  bufferCI.usage = bufferUsage;

  VmaAllocationCreateInfo allocationCI = {};
  allocationCI.usage = memoryUsage;

  VkBuffer buffer = VK_NULL_HANDLE;
  VmaAllocation allocation = VK_NULL_HANDLE;

  if (auto result = vmaCreateBuffer(sAllocator, &bufferCI, &allocationCI,
                                    &buffer, &allocation, nullptr);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(make_error_code(result), "Cannot create buffer"));
  }

  VkCommandBuffer commandBuffer;
  if (auto cb = BeginOneTimeSubmit(commandPool)) {
    commandBuffer = *cb;
  } else {
    return tl::unexpected(cb.error());
  }

  VkBufferCopy region = {};
  region.srcOffset = 0;
  region.dstOffset = 0;
  region.size = size;

  vkCmdCopyBuffer(commandBuffer, stagingBuffer, buffer, 1, &region);

  if (auto result = EndOneTimeSubmit(commandBuffer, commandPool, queue, fence);
      !result) {
    return tl::unexpected(result.error());
  }

  DestroyBuffer(stagingBuffer, stagingAllocation);

  Ensures(buffer != VK_NULL_HANDLE);
  Ensures(allocation != VK_NULL_HANDLE);

  IRIS_LOG_LEAVE();
  return std::make_tuple(buffer, allocation, size);
} // iris::Renderer::CreateBuffer

namespace iris::Renderer {

using namespace std::string_literals;

const TBuiltInResource DefaultTBuiltInResource = {
  /* .MaxLights = */ 32,
  /* .MaxClipPlanes = */ 6,
  /* .MaxTextureUnits = */ 32,
  /* .MaxTextureCoords = */ 32,
  /* .MaxVertexAttribs = */ 64,
  /* .MaxVertexUniformComponents = */ 4096,
  /* .MaxVaryingFloats = */ 64,
  /* .MaxVertexTextureImageUnits = */ 32,
  /* .MaxCombinedTextureImageUnits = */ 80,
  /* .MaxTextureImageUnits = */ 32,
  /* .MaxFragmentUniformComponents = */ 4096,
  /* .MaxDrawBuffers = */ 32,
  /* .MaxVertexUniformVectors = */ 128,
  /* .MaxVaryingVectors = */ 8,
  /* .MaxFragmentUniformVectors = */ 16,
  /* .MaxVertexOutputVectors = */ 16,
  /* .MaxFragmentInputVectors = */ 15,
  /* .MinProgramTexelOffset = */ -8,
  /* .MaxProgramTexelOffset = */ 7,
  /* .MaxClipDistances = */ 8,
  /* .MaxComputeWorkGroupCountX = */ 65535,
  /* .MaxComputeWorkGroupCountY = */ 65535,
  /* .MaxComputeWorkGroupCountZ = */ 65535,
  /* .MaxComputeWorkGroupSizeX = */ 1024,
  /* .MaxComputeWorkGroupSizeY = */ 1024,
  /* .MaxComputeWorkGroupSizeZ = */ 64,
  /* .MaxComputeUniformComponents = */ 1024,
  /* .MaxComputeTextureImageUnits = */ 16,
  /* .MaxComputeImageUniforms = */ 8,
  /* .MaxComputeAtomicCounters = */ 8,
  /* .MaxComputeAtomicCounterBuffers = */ 1,
  /* .MaxVaryingComponents = */ 60,
  /* .MaxVertexOutputComponents = */ 64,
  /* .MaxGeometryInputComponents = */ 64,
  /* .MaxGeometryOutputComponents = */ 128,
  /* .MaxFragmentInputComponents = */ 128,
  /* .MaxImageUnits = */ 8,
  /* .MaxCombinedImageUnitsAndFragmentOutputs = */ 8,
  /* .MaxCombinedShaderOutputResources = */ 8,
  /* .MaxImageSamples = */ 0,
  /* .MaxVertexImageUniforms = */ 0,
  /* .MaxTessControlImageUniforms = */ 0,
  /* .MaxTessEvaluationImageUniforms = */ 0,
  /* .MaxGeometryImageUniforms = */ 0,
  /* .MaxFragmentImageUniforms = */ 8,
  /* .MaxCombinedImageUniforms = */ 8,
  /* .MaxGeometryTextureImageUnits = */ 16,
  /* .MaxGeometryOutputVertices = */ 256,
  /* .MaxGeometryTotalOutputComponents = */ 1024,
  /* .MaxGeometryUniformComponents = */ 1024,
  /* .MaxGeometryVaryingComponents = */ 64,
  /* .MaxTessControlInputComponents = */ 128,
  /* .MaxTessControlOutputComponents = */ 128,
  /* .MaxTessControlTextureImageUnits = */ 16,
  /* .MaxTessControlUniformComponents = */ 1024,
  /* .MaxTessControlTotalOutputComponents = */ 4096,
  /* .MaxTessEvaluationInputComponents = */ 128,
  /* .MaxTessEvaluationOutputComponents = */ 128,
  /* .MaxTessEvaluationTextureImageUnits = */ 16,
  /* .MaxTessEvaluationUniformComponents = */ 1024,
  /* .MaxTessPatchComponents = */ 120,
  /* .MaxPatchVertices = */ 32,
  /* .MaxTessGenLevel = */ 64,
  /* .MaxViewports = */ 16,
  /* .MaxVertexAtomicCounters = */ 0,
  /* .MaxTessControlAtomicCounters = */ 0,
  /* .MaxTessEvaluationAtomicCounters = */ 0,
  /* .MaxGeometryAtomicCounters = */ 0,
  /* .MaxFragmentAtomicCounters = */ 8,
  /* .MaxCombinedAtomicCounters = */ 8,
  /* .MaxAtomicCounterBindings = */ 1,
  /* .MaxVertexAtomicCounterBuffers = */ 0,
  /* .MaxTessControlAtomicCounterBuffers = */ 0,
  /* .MaxTessEvaluationAtomicCounterBuffers = */ 0,
  /* .MaxGeometryAtomicCounterBuffers = */ 0,
  /* .MaxFragmentAtomicCounterBuffers = */ 1,
  /* .MaxCombinedAtomicCounterBuffers = */ 1,
  /* .MaxAtomicCounterBufferSize = */ 16384,
  /* .MaxTransformFeedbackBuffers = */ 4,
  /* .MaxTransformFeedbackInterleavedComponents = */ 64,
  /* .MaxCullDistances = */ 8,
  /* .MaxCombinedClipAndCullDistances = */ 8,
  /* .MaxSamples = */ 4,
  /* .maxMeshOutputVerticesNV = */ 256,
  /* .maxMeshOutputPrimitivesNV = */ 512,
  /* .maxMeshWorkGroupSizeX_NV = */ 32,
  /* .maxMeshWorkGroupSizeY_NV = */ 1,
  /* .maxMeshWorkGroupSizeZ_NV = */ 1,
  /* .maxTaskWorkGroupSizeX_NV = */ 32,
  /* .maxTaskWorkGroupSizeY_NV = */ 1,
  /* .maxTaskWorkGroupSizeZ_NV = */ 1,
  /* .maxMeshViewCountNV = */ 4,

  /* .limits = */
  {
    /* .nonInductiveForLoops = */ true,
    /* .whileLoops = */ true,
    /* .doWhileLoops = */ true,
    /* .generalUniformIndexing = */ true,
    /* .generalAttributeMatrixVectorIndexing = */ true,
    /* .generalVaryingIndexing = */ true,
    /* .generalSamplerIndexing = */ true,
    /* .generalVariableIndexing = */ true,
    /* .generalConstantMatrixVectorIndexing = */ true,
  }};

class DirStackIncluder : public glslang::TShader::Includer {
public:
  DirStackIncluder() noexcept = default;

  virtual IncludeResult* includeLocal(char const* headerName,
                                      char const* includerName,
                                      std::size_t inclusionDepth) override {
    return readLocalPath(headerName, includerName, inclusionDepth);
  }

  virtual IncludeResult* includeSystem(char const* headerName,
                                       char const* includerName
                                       [[maybe_unused]],
                                       std::size_t inclusionDepth
                                       [[maybe_unused]]) override {
    return readSystemPath(headerName);
  }

  virtual void releaseInclude(IncludeResult* result) override {
    if (result) {
      delete[] static_cast<char*>(result->userData);
      delete result;
    }
  }

  virtual void pushExternalLocalDirectory(std::string const& dir) {
    dirStack_.push_back(dir);
    numExternalLocalDirs_ = dirStack_.size();
  }

private:
  std::vector<std::string> dirStack_{};
  int numExternalLocalDirs_{0};

  virtual IncludeResult* readLocalPath(std::string const& headerName,
                                       std::string const& includerName,
                                       int depth) {
    // Discard popped include directories, and
    // initialize when at parse-time first level.
    dirStack_.resize(depth + numExternalLocalDirs_);

    if (depth == 1) dirStack_.back() = getDirectory(includerName);

    // Find a directory that works, using a reverse search of the include stack.
    for (auto& dir : dirStack_) {
      std::string path = dir + "/"s + headerName;
      std::replace(path.begin(), path.end(), '\\', '/');
      std::ifstream ifs(path.c_str(),
                        std::ios_base::binary | std::ios_base::ate);
      if (ifs) {
        dirStack_.push_back(getDirectory(path));
        return newIncludeResult(path, ifs, ifs.tellg());
      }
    }

    return nullptr;
  }

  virtual IncludeResult* readSystemPath(char const*) const {
    GetLogger()->error("including system headers not implemented");
    return nullptr;
  }

  virtual IncludeResult* newIncludeResult(std::string const& path,
                                          std::ifstream& ifs,
                                          int length) const {
    char* content = new char[length];
    ifs.seekg(0, ifs.beg);
    ifs.read(content, length);
    return new IncludeResult(path.c_str(), content, length, content);
  }

  // If no path markers, return current working directory.
  // Otherwise, strip file name and return path leading up to it.
  virtual std::string getDirectory(const std::string path) const {
    size_t last = path.find_last_of("/\\");
    return last == std::string::npos ? "." : path.substr(0, last);
  }
}; // class DirStackIncluder

[[nodiscard]] static tl::expected<std::vector<std::uint32_t>, std::string>
CompileShader(std::string_view source, VkShaderStageFlagBits shaderStage,
              filesystem::path const& path,
              gsl::span<std::string> macroDefinitions [[maybe_unused]],
              std::string const& entryPoint) {
  IRIS_LOG_ENTER();
  Expects(source.size() > 0);

  auto const lang = [&shaderStage]() {
    if ((shaderStage & VK_SHADER_STAGE_VERTEX_BIT)) {
      return EShLanguage::EShLangVertex;
    } else if ((shaderStage & VK_SHADER_STAGE_FRAGMENT_BIT)) {
      return EShLanguage::EShLangFragment;
    } else if ((shaderStage & VK_SHADER_STAGE_RAYGEN_BIT_NV)) {
      return EShLanguage::EShLangRayGenNV;
    } else if ((shaderStage & VK_SHADER_STAGE_ANY_HIT_BIT_NV)) {
      return EShLanguage::EShLangAnyHitNV;
    } else if ((shaderStage & VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV)) {
      return EShLanguage::EShLangClosestHitNV;
    } else if ((shaderStage & VK_SHADER_STAGE_INTERSECTION_BIT_NV)) {
      return EShLanguage::EShLangIntersectNV;
    } else if ((shaderStage & VK_SHADER_STAGE_MISS_BIT_NV)) {
      return EShLanguage::EShLangMissNV;
    } else if ((shaderStage & VK_SHADER_STAGE_CALLABLE_BIT_NV)) {
      return EShLanguage::EShLangCallableNV;
    } else if ((shaderStage & VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT)) {
      return EShLanguage::EShLangTessControl;
    } else if ((shaderStage & VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT)) {
      return EShLanguage::EShLangTessEvaluation;
    } else if ((shaderStage & VK_SHADER_STAGE_GEOMETRY_BIT)) {
      return EShLanguage::EShLangGeometry;
    } else {
      GetLogger()->critical("Unhandled shaderStage: {}", shaderStage);
      std::terminate();
    }
  }();

  char const* strings[] = {source.data()};
  int lengths[] = {static_cast<int>(source.size())};
  char const* names[] = {path.string().c_str()};

  glslang::TShader shader(lang);
  shader.setStringsWithLengthsAndNames(strings, lengths, names, 1);
  shader.setEntryPoint(entryPoint.c_str());
  shader.setEnvInput(glslang::EShSource::EShSourceGlsl, lang,
                     glslang::EShClient::EShClientVulkan, 101);
  shader.setEnvClient(glslang::EShClient::EShClientVulkan,
                      glslang::EShTargetClientVersion::EShTargetVulkan_1_1);
  shader.setEnvTarget(glslang::EShTargetLanguage::EShTargetSpv,
                      glslang::EShTargetLanguageVersion::EShTargetSpv_1_0);

  DirStackIncluder includer;
  includer.pushExternalLocalDirectory(kIRISContentDirectory);

  if (!shader.parse(&DefaultTBuiltInResource, 1, false,
                    EShMessages::EShMsgDefault, includer)) {
    return tl::unexpected(std::string(shader.getInfoLog()));
  }

  glslang::TProgram program;
  program.addShader(&shader);

  if (!program.link(EShMessages::EShMsgDefault)) {
    return tl::unexpected(std::string(program.getInfoLog()));
  }

  if (auto glsl = program.getIntermediate(lang)) {
    glslang::SpvOptions options;
    options.validate = true;
#ifndef NDEBUG
    options.generateDebugInfo = true;
#endif

    spv::SpvBuildLogger logger;
    std::vector<std::uint32_t> code;
    glslang::GlslangToSpv(*glsl, code, &logger, &options);

    Ensures(code.size() > 0);

    IRIS_LOG_LEAVE();
    return code;
  } else {
    return tl::unexpected(std::string(
      "cannot get glsl intermediate representation of compiled shader"));
  }
} // CompileShader

} // namespace iris::Renderer

tl::expected<VkShaderModule, std::system_error>
iris::Renderer::CompileShaderFromSource(std::string_view source,
                                        VkShaderStageFlagBits stage) noexcept {
  IRIS_LOG_ENTER();
  Expects(sDevice != VK_NULL_HANDLE);
  Expects(source.size() > 0);

  VkShaderModule module = VK_NULL_HANDLE;

  auto code = CompileShader(source, stage, "<inline>", {}, "main");
  if (!code) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(Error::kShaderCompileFailed, code.error()));
  }

  VkShaderModuleCreateInfo shaderModuleCI = {};
  shaderModuleCI.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  // codeSize is count of bytes, not count of words (which is what size() is)
  shaderModuleCI.codeSize = gsl::narrow_cast<std::uint32_t>(code->size()) * 4u;
  shaderModuleCI.pCode = code->data();

  if (auto result =
        vkCreateShaderModule(sDevice, &shaderModuleCI, nullptr, &module);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(make_error_code(result),
                                            "Cannot create shader module"));
  }

  Ensures(module != VK_NULL_HANDLE);

  IRIS_LOG_LEAVE();
  return module;
} // iris::Renderer::CompileShaderFromSource

tl::expected<VkShaderModule, std::system_error>
iris::Renderer::LoadShaderFromFile(filesystem::path const& path,
                                   VkShaderStageFlagBits stage) noexcept {
  IRIS_LOG_ENTER();
  Expects(sDevice != VK_NULL_HANDLE);
  Expects(!path.empty());

  auto bytes = iris::io::ReadFile(path);

  if (!bytes) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(bytes.error());
  }

  auto sm = iris::Renderer::CompileShaderFromSource(
    {reinterpret_cast<char const*>(bytes->data()), bytes->size()}, stage);
  if (!sm) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(sm.error());
  }

  IRIS_LOG_LEAVE();
  return *sm;
} // iris::Renderer::LoadShaderFromFile

tl::expected<iris::Renderer::AccelerationStructure, std::system_error>
iris::Renderer::CreateAccelerationStructure(
  VkAccelerationStructureCreateInfoNV*
    pAccelerationStructureCreateInfo) noexcept {
  IRIS_LOG_ENTER();
  Expects(sDevice != VK_NULL_HANDLE);
  Expects(sAllocator != VK_NULL_HANDLE);
  Expects(pAccelerationStructureCreateInfo != nullptr);

  AccelerationStructure structure;

  if (auto result = vkCreateAccelerationStructureNV(
        sDevice, pAccelerationStructureCreateInfo, nullptr,
        &structure.structure);
      result != VK_SUCCESS) {
    return tl::unexpected(std::system_error(
      make_error_code(result), "Cannot create acceleration structure"));
  }

  VkMemoryRequirements2KHR memoryRequirements = {};
  memoryRequirements.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2_KHR;

  VkAccelerationStructureMemoryRequirementsInfoNV memoryRequirementsInfo = {};
  memoryRequirementsInfo.sType =
    VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_NV;
  memoryRequirementsInfo.accelerationStructure = structure.structure;
  memoryRequirementsInfo.type =
    VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_OBJECT_NV;
  vkGetAccelerationStructureMemoryRequirementsNV(
    sDevice, &memoryRequirementsInfo, &memoryRequirements);

  VmaAllocationCreateInfo allocationCI = {};
  allocationCI.flags = VMA_MEMORY_USAGE_GPU_ONLY;
  allocationCI.memoryTypeBits =
    memoryRequirements.memoryRequirements.memoryTypeBits;

  if (auto result =
        vmaAllocateMemory(sAllocator, &memoryRequirements.memoryRequirements,
                          &allocationCI, &structure.allocation, nullptr);
      result != VK_SUCCESS) {
    vkDestroyAccelerationStructureNV(sDevice, structure.structure, nullptr);
    return tl::unexpected(std::system_error(
      iris::Renderer::make_error_code(result), "Cannot allocate memory"));
  }

  VmaAllocationInfo allocationInfo;
  vmaGetAllocationInfo(sAllocator, structure.allocation, &allocationInfo);

  VkBindAccelerationStructureMemoryInfoNV bindAccelerationStructureMemoryInfo =
    {};
  bindAccelerationStructureMemoryInfo.sType =
    VK_STRUCTURE_TYPE_BIND_ACCELERATION_STRUCTURE_MEMORY_INFO_NV;
  bindAccelerationStructureMemoryInfo.accelerationStructure =
    structure.structure;
  bindAccelerationStructureMemoryInfo.memory = allocationInfo.deviceMemory;
  bindAccelerationStructureMemoryInfo.memoryOffset = 0;

  if (auto result = vkBindAccelerationStructureMemoryNV(
        sDevice, 1, &bindAccelerationStructureMemoryInfo);
      result != VK_SUCCESS) {
    vmaFreeMemory(sAllocator, structure.allocation);
    vkDestroyAccelerationStructureNV(sDevice, structure.structure, nullptr);
    return tl::unexpected(std::system_error(
      make_error_code(result), "Cannot bind memory to acceleration structure"));
  }

  Ensures(structure.structure != VK_NULL_HANDLE);
  Ensures(structure.allocation != VK_NULL_HANDLE);

  IRIS_LOG_LEAVE();
  return structure;
} // iris::Renderer::CreateAccelerationStructure

tl::expected<std::pair<VkPipelineLayout, VkPipeline>, std::system_error>
iris::Renderer::CreateGraphicsPipeline(
  gsl::span<const Shader> shaders,
  gsl::span<const VkVertexInputBindingDescription>
    vertexInputBindingDescriptions,
  gsl::span<const VkVertexInputAttributeDescription>
    vertexInputAttributeDescriptions,
  VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI,
  VkPipelineViewportStateCreateInfo viewportStateCI,
  VkPipelineRasterizationStateCreateInfo rasterizationStateCI,
  VkPipelineMultisampleStateCreateInfo multisampleStateCI,
  VkPipelineDepthStencilStateCreateInfo depthStencilStateCI,
  gsl::span<const VkPipelineColorBlendAttachmentState>
    colorBlendAttachmentStates,
  gsl::span<const VkDynamicState> dynamicStates,
  std::uint32_t renderPassSubpass,
  gsl::span<const VkDescriptorSetLayout> descriptorSetLayouts) noexcept {
  IRIS_LOG_ENTER();
  Expects(sDevice != VK_NULL_HANDLE);
  Expects(sRenderPass != VK_NULL_HANDLE);
  Expects(sGlobalDescriptorSetLayout != VK_NULL_HANDLE);

  VkPipelineLayout layout{VK_NULL_HANDLE};
  VkPipeline pipeline{VK_NULL_HANDLE};

  absl::InlinedVector<VkDescriptorSetLayout, 8> allDescriptorSetLayouts;
  allDescriptorSetLayouts.push_back(sGlobalDescriptorSetLayout);
  std::copy_n(std::begin(descriptorSetLayouts), std::size(descriptorSetLayouts),
              std::back_inserter(allDescriptorSetLayouts));

  VkPushConstantRange pushConstantRange = {};
  pushConstantRange.stageFlags =
    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  pushConstantRange.offset = 0;
  pushConstantRange.size = sizeof(PushConstants);

  VkPipelineLayoutCreateInfo pipelineLayoutCI = {};
  pipelineLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutCI.setLayoutCount =
    gsl::narrow_cast<std::uint32_t>(allDescriptorSetLayouts.size());
  pipelineLayoutCI.pSetLayouts = allDescriptorSetLayouts.data();
  pipelineLayoutCI.pushConstantRangeCount = 1;
  pipelineLayoutCI.pPushConstantRanges = &pushConstantRange;

  if (auto result =
        vkCreatePipelineLayout(sDevice, &pipelineLayoutCI, nullptr, &layout);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(make_error_code(result),
                                            "Cannot create pipeline layout"));
  }

  absl::FixedArray<VkPipelineShaderStageCreateInfo> shaderStageCIs(
    shaders.size());
  std::transform(shaders.begin(), shaders.end(), shaderStageCIs.begin(),
                 [](Shader const& shader) {
                   return VkPipelineShaderStageCreateInfo{
                     VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                     nullptr,
                     0,
                     shader.stage,
                     shader.handle,
                     "main",
                     nullptr};
                 });

  VkPipelineVertexInputStateCreateInfo vertexInputStateCI = {};
  vertexInputStateCI.sType =
    VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertexInputStateCI.vertexBindingDescriptionCount =
    gsl::narrow_cast<std::uint32_t>(vertexInputBindingDescriptions.size());
  vertexInputStateCI.pVertexBindingDescriptions =
    vertexInputBindingDescriptions.data();
  vertexInputStateCI.vertexAttributeDescriptionCount =
    gsl::narrow_cast<std::uint32_t>(vertexInputAttributeDescriptions.size());
  vertexInputStateCI.pVertexAttributeDescriptions =
    vertexInputAttributeDescriptions.data();

  VkPipelineColorBlendStateCreateInfo colorBlendStateCI = {};
  colorBlendStateCI.sType =
    VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlendStateCI.attachmentCount =
    gsl::narrow_cast<std::uint32_t>(colorBlendAttachmentStates.size());
  colorBlendStateCI.pAttachments = colorBlendAttachmentStates.data();

  VkPipelineDynamicStateCreateInfo dynamicStateCI = {};
  dynamicStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamicStateCI.dynamicStateCount =
    gsl::narrow_cast<uint32_t>(dynamicStates.size());
  dynamicStateCI.pDynamicStates = dynamicStates.data();

  VkGraphicsPipelineCreateInfo graphicsPipelineCI = {};
  graphicsPipelineCI.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  graphicsPipelineCI.stageCount =
    gsl::narrow_cast<std::uint32_t>(shaderStageCIs.size());
  graphicsPipelineCI.pStages = shaderStageCIs.data();
  graphicsPipelineCI.pVertexInputState = &vertexInputStateCI;
  graphicsPipelineCI.pInputAssemblyState = &inputAssemblyStateCI;
  graphicsPipelineCI.pViewportState = &viewportStateCI;
  graphicsPipelineCI.pRasterizationState = &rasterizationStateCI;
  graphicsPipelineCI.pMultisampleState = &multisampleStateCI;
  graphicsPipelineCI.pDepthStencilState = &depthStencilStateCI;
  graphicsPipelineCI.pColorBlendState = &colorBlendStateCI;
  graphicsPipelineCI.pDynamicState = &dynamicStateCI;
  graphicsPipelineCI.layout = layout;
  graphicsPipelineCI.renderPass = sRenderPass;
  graphicsPipelineCI.subpass = renderPassSubpass;

  if (auto result = vkCreateGraphicsPipelines(
        sDevice, VK_NULL_HANDLE, 1, &graphicsPipelineCI, nullptr, &pipeline);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(make_error_code(result),
                                            "Cannot create graphics pipeline"));
  }

  Ensures(layout != VK_NULL_HANDLE);
  Ensures(pipeline != VK_NULL_HANDLE);

  IRIS_LOG_LEAVE();
  return std::make_pair(layout, pipeline);
} // iris::Renderer::CreateGraphicsPipeline
