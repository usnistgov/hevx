#ifndef HEV_IRIS_COMPONENTS_MATERIAL_H_
#define HEV_IRIS_COMPONENTS_MATERIAL_H_

#include "absl/container/inlined_vector.h"
#include "glm/mat4x4.hpp"
#include "glm/vec4.hpp"
#include "iris/buffer.h"
#include "iris/image.h"
#include "iris/pipeline.h"
#include "iris/vulkan.h"
#include <cstdint>

namespace iris::Renderer::Component {

struct Material {
  static constexpr std::size_t const kMaxTextures = 8;
  absl::InlinedVector<Image, kMaxTextures> textures{};
  absl::InlinedVector<VkImageView, kMaxTextures> textureViews{};
  absl::InlinedVector<VkSampler, kMaxTextures> textureSamplers{};

  Buffer materialBuffer{};

  static constexpr std::size_t const kMaxVertexBindings = 4;
  absl::InlinedVector<VkVertexInputBindingDescription, kMaxVertexBindings>
    vertexInputBindingDescriptions;

  static constexpr std::size_t const kMaxVertexAttributes = 4;
  absl::InlinedVector<VkVertexInputAttributeDescription, kMaxVertexAttributes>
    vertexInputAttributeDescriptions{};

  VkPrimitiveTopology topology{VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};

  VkPolygonMode polygonMode{VK_POLYGON_MODE_FILL};
  VkCullModeFlags cullMode{VK_CULL_MODE_BACK_BIT};

  absl::InlinedVector<VkDescriptorSetLayoutBinding, kMaxTextures + 1>
    descriptorSetLayoutBindings{};

  VkDescriptorSetLayout descriptorSetLayout{VK_NULL_HANDLE};
  VkDescriptorSet descriptorSet{VK_NULL_HANDLE};

  Pipeline pipeline{};
}; // struct Material

inline bool operator==(Material const& lhs, Material const& rhs) noexcept {
  return lhs.pipeline.pipeline == rhs.pipeline.pipeline;
}

} // namespace iris::Renderer::Component

#endif // HEV_IRIS_COMPONENTS_MATERIAL_H_
