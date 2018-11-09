#include "renderer/buffer.h"
#include "renderer/impl.h"
#include "logging.h"

tl::expected<std::pair<VkBuffer, VmaAllocation>, std::system_error>
iris::Renderer::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags bufferUsage,
             VmaMemoryUsage memoryUsage) noexcept {
  IRIS_LOG_ENTER();
  VkResult result;

  VkBufferCreateInfo bufferCI = {};
  bufferCI.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferCI.size = size;
  bufferCI.usage = bufferUsage;

  VmaAllocationCreateInfo allocationCI = {};
  allocationCI.usage = memoryUsage;

  VkBuffer buffer;
  VmaAllocation allocation;
  result = vmaCreateBuffer(sAllocator, &bufferCI, &allocationCI, &buffer,
                           &allocation, nullptr);
  if (result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(make_error_code(result), "Error creating buffer"));
  }

  IRIS_LOG_LEAVE();
  return std::make_pair(buffer, allocation);
} // iris::Renderer::CreateBuffer
