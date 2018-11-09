#ifndef HEV_IRIS_RENDERER_BUFFER_H_
#define HEV_IRIS_RENDERER_BUFFER_H_

#include "renderer/vulkan.h"
#include "tl/expected.hpp"
#include <system_error>

namespace iris::Renderer {

tl::expected<std::pair<VkBuffer, VmaAllocation>, std::system_error>
CreateBuffer(VkDeviceSize size, VkBufferUsageFlags bufferUsage,
             VmaMemoryUsage memoryUsage) noexcept;

} // namespace iris::Renderer

#endif // HEV_IRIS_RENDERER_BUFFER_H_
