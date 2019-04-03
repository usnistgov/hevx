#include "buffer.h"

#include "config.h"

#include "error.h"
#include "logging.h"
#include "renderer.h"
#include "renderer_util.h"

template <>
tl::expected<void*, std::system_error>
iris::Renderer::Buffer::Map() noexcept {
  void* ptr;
  if (auto result = vmaMapMemory(sAllocator, allocation, &ptr);
      result != VK_SUCCESS) {
    return tl::unexpected(
      std::system_error(make_error_code(result), "Cannot map memory"));
  }
  return ptr;
} // iris::Buffer::Renderer::Map

void iris::Renderer::Buffer::Unmap() noexcept {
  vmaUnmapMemory(sAllocator, allocation);
} // iris::Renderer::Buffer::Unmap

tl::expected<iris::Renderer::Buffer, std::system_error>
iris::Renderer::AllocateBuffer(VkDeviceSize size,
                               VkBufferUsageFlags bufferUsage,
                               VmaMemoryUsage memoryUsage) noexcept {
  IRIS_LOG_ENTER();
  Expects(sAllocator != VK_NULL_HANDLE);
  Expects(size > 0);

  Buffer buffer;
  buffer.size = size;

  VkBufferCreateInfo bufferCI = {};
  bufferCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferCI.size = buffer.size;
  bufferCI.usage = bufferUsage;

  VmaAllocationCreateInfo allocationCI = {};
  allocationCI.usage = memoryUsage;

  if (auto result =
        vmaCreateBuffer(sAllocator, &bufferCI, &allocationCI, &buffer.buffer,
                        &buffer.allocation, nullptr);
      result != VK_SUCCESS) {
    return tl::unexpected(
      std::system_error(make_error_code(result), "Cannot create buffer"));
  }

  Ensures(buffer.buffer != VK_NULL_HANDLE);
  Ensures(buffer.allocation != VK_NULL_HANDLE);

  IRIS_LOG_LEAVE();
  return buffer;
} // iris::Renderer::AllocateBuffer

tl::expected<iris::Renderer::Buffer, std::system_error>
iris::Renderer::ReallocateBuffer(Buffer oldBuffer, VkDeviceSize newSize,
                                 VkBufferUsageFlags bufferUsage,
                                 VmaMemoryUsage memoryUsage) noexcept {
  IRIS_LOG_ENTER();
  Expects(sAllocator != VK_NULL_HANDLE);
  Expects(newSize > 0);

  if (oldBuffer.buffer != VK_NULL_HANDLE &&
      oldBuffer.allocation != VK_NULL_HANDLE) {
    if (oldBuffer.size >= newSize) {
      IRIS_LOG_LEAVE();
      return oldBuffer;
    }

    if (auto result =
          vmaResizeAllocation(sAllocator, oldBuffer.allocation, newSize);
        result == VK_SUCCESS) {
      IRIS_LOG_LEAVE();
      return oldBuffer;
    }
  }

  Buffer newBuffer;
  newBuffer.size = newSize;

  VkBufferCreateInfo bufferCI = {};
  bufferCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferCI.size = newBuffer.size;
  bufferCI.usage = bufferUsage;

  VmaAllocationCreateInfo allocationCI = {};
  allocationCI.usage = memoryUsage;

  if (auto result =
        vmaCreateBuffer(sAllocator, &bufferCI, &allocationCI, &newBuffer.buffer,
                        &newBuffer.allocation, nullptr);
      result != VK_SUCCESS) {
    return tl::unexpected(
      std::system_error(make_error_code(result), "Cannot create buffer"));
  }

  DestroyBuffer(oldBuffer);

  Ensures(newBuffer.buffer != VK_NULL_HANDLE);
  Ensures(newBuffer.allocation != VK_NULL_HANDLE);

  IRIS_LOG_LEAVE();
  return newBuffer;
} // iris::Renderer::ReallocateBuffer

tl::expected<iris::Renderer::Buffer, std::system_error>
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

  auto staging = AllocateBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                VMA_MEMORY_USAGE_CPU_TO_GPU);
  if (!staging) {
    using namespace std::string_literals;
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(staging.error().code(), "Cannot create staging buffer: "s +
                                              staging.error().what()));
  }

  if (auto ptr = staging->Map<std::byte*>()) {
    std::memcpy(*ptr, data, size);
    staging->Unmap();
  } else {
    using namespace std::string_literals;
    DestroyBuffer(*staging);
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(
      ptr.error().code(), "Cannot map staging buffer: "s + ptr.error().what()));
  }

  Buffer buffer;
  buffer.size = size;

  VkBufferCreateInfo bufferCI = {};
  bufferCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferCI.size = buffer.size;
  bufferCI.usage = bufferUsage;

  VmaAllocationCreateInfo allocationCI = {};
  allocationCI.usage = memoryUsage;

  if (auto result =
        vmaCreateBuffer(sAllocator, &bufferCI, &allocationCI, &buffer.buffer,
                        &buffer.allocation, nullptr);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    DestroyBuffer(*staging);
    return tl::unexpected(
      std::system_error(make_error_code(result), "Cannot create buffer"));
  }

  VkCommandBuffer commandBuffer;
  if (auto cb = BeginOneTimeSubmit(commandPool)) {
    commandBuffer = *cb;
  } else {
    IRIS_LOG_LEAVE();
    DestroyBuffer(*staging);
    return tl::unexpected(cb.error());
  }

  VkBufferCopy region = {};
  region.srcOffset = 0;
  region.dstOffset = 0;
  region.size = buffer.size;

  vkCmdCopyBuffer(commandBuffer, staging->buffer, buffer.buffer, 1, &region);

  if (auto result = EndOneTimeSubmit(commandBuffer, commandPool, queue, fence);
      !result) {
    IRIS_LOG_LEAVE();
    DestroyBuffer(*staging);
    return tl::unexpected(result.error());
  }

  DestroyBuffer(*staging);

  Ensures(buffer.buffer != VK_NULL_HANDLE);
  Ensures(buffer.allocation != VK_NULL_HANDLE);

  IRIS_LOG_LEAVE();
  return buffer;
} // iris::Renderer::CreateBuffer

void iris::Renderer::DestroyBuffer(Buffer buffer) noexcept {
  vmaDestroyBuffer(sAllocator, buffer.buffer, buffer.allocation);
} // iris::Renderer::DestroyBuffer

