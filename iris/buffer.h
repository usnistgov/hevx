#ifndef HEV_IRIS_BUFFER_H_
#define HEV_IRIS_BUFFER_H_

#include "iris/vulkan.h"

namespace iris {

struct Buffer {
  VkBuffer buffer{VK_NULL_HANDLE};
  VmaAllocation allocation{VK_NULL_HANDLE};
  VkDeviceSize size{0};

  template <class T>
  tl::expected<T, std::system_error> Map() noexcept {
    if (auto ptr = Map<void*>()) return reinterpret_cast<T>(*ptr);
    else return tl::unexpected(ptr.error());
  }

  void Unmap() noexcept;
}; // struct Buffer

template <>
tl::expected<void*, std::system_error> Buffer::Map() noexcept;

[[nodiscard]] tl::expected<Buffer, std::system_error>
AllocateBuffer(VkDeviceSize size, VkBufferUsageFlags bufferUsage,
               VmaMemoryUsage memoryUsage) noexcept;

[[nodiscard]] tl::expected<Buffer, std::system_error>
ReallocateBuffer(Buffer oldBuffer, VkDeviceSize newSize,
                 VkBufferUsageFlags bufferUsage,
                 VmaMemoryUsage memoryUsage) noexcept;

[[nodiscard]] tl::expected<Buffer, std::system_error>
CreateBuffer(VkCommandPool commandPool, VkQueue queue, VkFence fence,
             VkBufferUsageFlags bufferUsage, VmaMemoryUsage memoryUsage,
             VkDeviceSize size, gsl::not_null<std::byte*> data) noexcept;

void DestroyBuffer(Buffer buffer) noexcept;

} // namespace iris

#endif // HEV_IRIS_BUFFER_H_

