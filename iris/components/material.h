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
  if (lhs.vertexInputBindingDescriptions.size() !=
      rhs.vertexInputBindingDescriptions.size()) {
    return false;
  }

  for (auto&& lIBD : lhs.vertexInputBindingDescriptions) {
    bool found = false;

    for (auto&& rIBD : rhs.vertexInputBindingDescriptions) {
      found = (lIBD.binding == rIBD.binding) && (lIBD.stride == rIBD.stride) &&
              (lIBD.inputRate == rIBD.inputRate);
      if (found) break;
    }

    // !found means we didn't find lIBD in rhs.vertexInputBindingDescriptions
    if (!found) return false;
  }

  if (lhs.vertexInputAttributeDescriptions.size() !=
      rhs.vertexInputAttributeDescriptions.size()) {
    return false;
  }

  for (auto&& lIAD : lhs.vertexInputAttributeDescriptions) {
    bool found = false;

    for (auto&& rIAD : rhs.vertexInputAttributeDescriptions) {
      found = (lIAD.location == rIAD.location) &&
              (lIAD.binding == rIAD.binding) && (lIAD.format == rIAD.format) &&
              (lIAD.offset == rIAD.offset);
      if (found) break;
    }

    // !found means we didn't find lIAD in rhs.vertexInputAttributeDescriptions
    if (!found) return false;
  }

  if (rhs.descriptorSetLayoutBindings.size() !=
      lhs.descriptorSetLayoutBindings.size()) {
    return false;
  }

  for (auto&& lSLB : lhs.descriptorSetLayoutBindings) {
    bool found = false;

    for (auto&& rSLB : rhs.descriptorSetLayoutBindings) {
      found = (lSLB.binding == rSLB.binding) &&
              (lSLB.descriptorType == rSLB.descriptorType) &&
              (lSLB.descriptorCount == rSLB.descriptorCount) &&
              (lSLB.stageFlags == rSLB.stageFlags);
      if (found) break;
    }
  }

  return (lhs.topology == rhs.topology) &&
         (lhs.polygonMode == rhs.polygonMode) && (lhs.cullMode == rhs.cullMode);
}

} // namespace iris::Renderer::Component

#endif // HEV_IRIS_COMPONENTS_MATERIAL_H_
