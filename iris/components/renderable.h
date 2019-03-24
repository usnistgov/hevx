#ifndef HEV_IRIS_COMPONENTS_RENDERABLE_H_
#define HEV_IRIS_COMPONENTS_RENDERABLE_H_

#include "absl/container/inlined_vector.h"
#include "glm/mat4x4.hpp"
#include "iris/vulkan.h"
#include <cstdint>

namespace iris::Renderer::Component {

struct Renderable {
  static constexpr std::size_t const kMaxTextures = 8;

  VkPipelineLayout pipelineLayout{VK_NULL_HANDLE};
  VkPipeline pipeline{VK_NULL_HANDLE};
  VkDescriptorSet descriptorSet{VK_NULL_HANDLE};

  absl::InlinedVector<VkImage, kMaxTextures> images;
  absl::InlinedVector<VmaAllocation, kMaxTextures> allocations;
  absl::InlinedVector<VkImageView, kMaxTextures> views;
  absl::InlinedVector<VkSampler, kMaxTextures> samplers;

  VkShaderStageFlags pushConstantsStages{};
  void* pushConstants{nullptr};
  VkDeviceSize pushConstantsSize{0};

  VkDeviceSize uniformBufferSize{0};
  VkBuffer uniformBuffer{VK_NULL_HANDLE};
  VmaAllocation uniformBufferAllocation{VK_NULL_HANDLE};

  VkDeviceSize vertexBufferSize{0};
  VkDeviceSize vertexBufferBindingOffset{0};
  VkBuffer vertexBuffer{VK_NULL_HANDLE};
  VmaAllocation vertexBufferAllocation{VK_NULL_HANDLE};

  VkDeviceSize indexBufferSize{0};
  VkDeviceSize indexBufferBindingOffset{0};
  VkBuffer indexBuffer{VK_NULL_HANDLE};
  VmaAllocation indexBufferAllocation{VK_NULL_HANDLE};

  glm::mat4 modelMatrix{1.f};

  VkIndexType indexType{VK_INDEX_TYPE_UINT32};
  std::uint32_t numIndices{0};
  std::uint32_t instanceCount{1};
  std::uint32_t firstIndex{0};
  std::uint32_t vertexOffset{0};
  std::uint32_t firstInstance{0};

  std::uint32_t numVertices{0};
  std::uint32_t firstVertex{0};

  std::uint32_t numTextures{0};
}; // struct Renderable

} // namespace iris::Renderer::Component

#endif // HEV_IRIS_COMPONENTS_RENDERABLE_H_
