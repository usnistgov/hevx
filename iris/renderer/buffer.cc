#include "renderer/buffer.h"
#include "renderer/impl.h"
#include "logging.h"

tl::expected<iris::Renderer::Buffer, std::system_error>
iris::Renderer::Buffer::Create(VkDeviceSize size,
                               VkBufferUsageFlags bufferUsage,
                               VmaMemoryUsage memoryUsage,
                               std::string name) noexcept {
  IRIS_LOG_ENTER();
  VkResult result;
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

  result = vmaCreateBuffer(sAllocator, &bufferCI, &allocationCI, &buffer.buffer,
                           &buffer.allocation, nullptr);
  if (result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(make_error_code(result), "Error creating buffer"));
  }

  if (!name.empty()) {
    NameObject(VK_OBJECT_TYPE_BUFFER, buffer.buffer, name.c_str());
  }

  buffer.size = size;
  buffer.name = std::move(name);

  IRIS_LOG_LEAVE();
  return std::move(buffer);
} // iris::Renderer::Buffer::Create

iris::Renderer::Buffer::Buffer(Buffer&& other) noexcept
  : size(other.size)
  , buffer(other.buffer)
  , allocation(other.allocation)
  , name(std::move(other.name)) {
  other.buffer = VK_NULL_HANDLE;
  other.allocation = VK_NULL_HANDLE;
} // iris::Renderer::Buffer::Buffer

iris::Renderer::Buffer& iris::Renderer::Buffer::operator=(Buffer&& rhs) noexcept {
  if (this == &rhs) return *this;

  size = rhs.size;
  buffer = rhs.buffer;
  allocation = rhs.allocation;
  name = std::move(rhs.name);

  rhs.buffer = VK_NULL_HANDLE;
  rhs.allocation = VK_NULL_HANDLE;

  return *this;
} // iris::Renderer::Buffer::operator=

iris::Renderer::Buffer::~Buffer() noexcept {
  if (buffer == VK_NULL_HANDLE || allocation == VK_NULL_HANDLE) return;
  IRIS_LOG_ENTER();

  vmaDestroyBuffer(sAllocator, buffer, allocation);

  IRIS_LOG_LEAVE();
} // iris::Renderer::Buffer::~Buffer

