#ifndef HEV_IRIS_RENDERER_BUFFER_H_
#define HEV_IRIS_RENDERER_BUFFER_H_

#include "renderer/vulkan.h"
#include "tl/expected.hpp"

namespace iris::Renderer {

tl::expected<std::pair<VkBuffer, VmaAllocation>, std::error_code>
CreateBuffer(VkDeviceSize size, VkBufferUsageFlags bufferUsage,
             VmaMemoryUsage memoryUsage) noexcept;

} // namespace iris::Renderer

#endif // HEV_IRIS_RENDERER_BUFFER_H_
