#include "renderer/buffer.h"
#include "logging.h"

tl::expected<iris::Renderer::Buffer, std::system_error>
iris::Renderer::Buffer::Create(VkDeviceSize size,
                               VkBufferUsageFlags bufferUsage,
                               VmaMemoryUsage memoryUsage,
                               std::string name) noexcept {
  IRIS_LOG_ENTER();
  Expects(sDevice != VK_NULL_HANDLE);

  Buffer buffer;

  VkBufferCreateInfo bufferCI = {};
  bufferCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferCI.size = size;
  bufferCI.usage = bufferUsage;

  VmaAllocationCreateInfo allocationCI = {};
  allocationCI.usage = memoryUsage;

  if (!name.empty()) {
    allocationCI.flags = VMA_ALLOCATION_CREATE_USER_DATA_COPY_STRING_BIT;
    allocationCI.pUserData = name.data();
  }

  if (auto result =
        vmaCreateBuffer(sAllocator, &bufferCI, &allocationCI, &buffer.handle,
                        &buffer.allocation, nullptr);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(make_error_code(result), "Error creating buffer"));
  }

  if (!name.empty()) {
    NameObject(VK_OBJECT_TYPE_BUFFER, buffer.handle, name.c_str());
  }

  buffer.size = size;
  buffer.name = std::move(name);

  Ensures(buffer.handle != VK_NULL_HANDLE);
  IRIS_LOG_LEAVE();
  return std::move(buffer);
} // iris::Renderer::Buffer::Create

tl::expected<iris::Renderer::Buffer, std::system_error>
iris::Renderer::Buffer::CreateFromMemory(VkDeviceSize size,
                                         VkBufferUsageFlags bufferUsage,
                                         VmaMemoryUsage memoryUsage,
                                         gsl::not_null<void*> data,
                                         std::string name,
                                         VkCommandPool commandPool) noexcept {
  IRIS_LOG_ENTER();
  Expects(sDevice != VK_NULL_HANDLE);
  Expects(commandPool != VK_NULL_HANDLE);

  Buffer buffer;

  Buffer stagingBuffer;
  if (auto sb = Buffer::Create(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                               VMA_MEMORY_USAGE_CPU_TO_GPU)) {
    stagingBuffer = std::move(*sb);
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(sb.error().code(), "Cannot create staging buffer"));
  }

  if (auto p = stagingBuffer.Map<unsigned char*>()) {
    std::memcpy(*p, data, size);
  } else {
    using namespace std::string_literals;
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(
      p.error().code(), "Cannot map staging buffer: "s + p.error().what()));
  }

  stagingBuffer.Unmap();

  VkBufferCreateInfo bufferCI = {};
  bufferCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferCI.size = size;
  bufferCI.usage = bufferUsage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

  VmaAllocationCreateInfo allocationCI = {};
  allocationCI.usage = memoryUsage;

  if (!name.empty()) {
    allocationCI.flags = VMA_ALLOCATION_CREATE_USER_DATA_COPY_STRING_BIT;
    allocationCI.pUserData = name.data();
  }

  if (auto result =
        vmaCreateBuffer(sAllocator, &bufferCI, &allocationCI, &buffer.handle,
                        &buffer.allocation, nullptr);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(make_error_code(result), "Error creating buffer"));
  }

  VkCommandBuffer commandBuffer;
  if (auto cb = BeginOneTimeSubmit(commandPool)) {
    commandBuffer = *cb;
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(cb.error());
  }

  VkBufferCopy region = {};
  region.srcOffset = 0;
  region.dstOffset = 0;
  region.size = size;

  vkCmdCopyBuffer(commandBuffer, stagingBuffer, buffer, 1, &region);

  if (auto error = EndOneTimeSubmit(commandBuffer, commandPool); error.code()) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(error);
  }

  if (!name.empty()) {
    NameObject(VK_OBJECT_TYPE_BUFFER, buffer.handle, name.c_str());
  }

  buffer.size = size;
  buffer.name = std::move(name);

  Ensures(buffer.handle != VK_NULL_HANDLE);
  IRIS_LOG_LEAVE();
  return std::move(buffer);
} // iris::Renderer::Buffer::CreateFromMemory

iris::Renderer::Buffer::Buffer(Buffer&& other) noexcept
  : size(other.size)
  , handle(other.handle)
  , allocation(other.allocation)
  , name(std::move(other.name)) {
  other.handle = VK_NULL_HANDLE;
  other.allocation = VK_NULL_HANDLE;
} // iris::Renderer::Buffer::Buffer

iris::Renderer::Buffer& iris::Renderer::Buffer::operator=(Buffer&& rhs) noexcept {
  if (this == &rhs) return *this;

  size = rhs.size;
  handle = rhs.handle;
  allocation = rhs.allocation;
  name = std::move(rhs.name);

  rhs.handle = VK_NULL_HANDLE;
  rhs.allocation = VK_NULL_HANDLE;

  return *this;
} // iris::Renderer::Buffer::operator=

iris::Renderer::Buffer::~Buffer() noexcept {
  if (handle == VK_NULL_HANDLE) return;
  IRIS_LOG_ENTER();

  vmaDestroyBuffer(sAllocator, handle, allocation);

  IRIS_LOG_LEAVE();
} // iris::Renderer::Buffer::~Buffer

