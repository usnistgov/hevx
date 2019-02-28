#ifndef HEV_IRIS_COMPONENTS_RENDERABLE_H_
#define HEV_IRIS_COMPONENTS_RENDERABLE_H_

#include "iris/vulkan.h"
#include <cstdint>

namespace iris::Renderer::Component {

struct Renderable {
  VkPipeline pipeline{VK_NULL_HANDLE};
  VkPipelineLayout pipelineLayout{VK_NULL_HANDLE};
  VkDescriptorSet descriptorSet{VK_NULL_HANDLE};

  void* pushConstants{nullptr};
  std::uint32_t pushConstantsSize{0};
  VkBuffer uniformBuffer{VK_NULL_HANDLE};

  VkDeviceSize vertexBufferSize{0};
  VkDeviceSize vertexBufferBindingOffset{0};
  VkBuffer vertexBuffer{VK_NULL_HANDLE};
  VmaAllocation vertexBufferAllocation{VK_NULL_HANDLE};

  VkDeviceSize indexBufferSize{0};
  VkDeviceSize indexBufferBindingOffset{0};
  VkBuffer indexBuffer{VK_NULL_HANDLE};
  VmaAllocation indexBufferAllocation{VK_NULL_HANDLE};

  VkIndexType indexType{VK_INDEX_TYPE_UINT32};
  std::uint32_t numIndices{0};
  std::uint32_t instanceCount{1};
  std::uint32_t firstIndex{0};
  std::uint32_t vertexOffset{0};
  std::uint32_t firstInstance{0};

  std::uint32_t numVertices{0};
  std::uint32_t firstVertex{0};
}; // struct Renderable

} // namespace iris::Renderer::Component

#endif // HEV_IRIS_COMPONENTS_RENDERABLE_H_
