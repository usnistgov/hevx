#ifndef HEV_IRIS_COMPONENTS_TRACEABLE_H_
#define HEV_IRIS_COMPONENTS_TRACEABLE_H_

#include "absl/container/inlined_vector.h"
#include "glm/mat4x4.hpp"
#include "glm/vec4.hpp"
#include "iris/acceleration_structure.h"
#include "iris/buffer.h"
#include "iris/image.h"
#include "iris/pipeline.h"
#include "iris/vulkan.h"
#include <cstdint>

namespace iris::Renderer::Component {

struct Traceable {
  Pipeline pipeline{};

  VkDescriptorSetLayout descriptorSetLayout{VK_NULL_HANDLE};
  VkDescriptorSet descriptorSet{VK_NULL_HANDLE};

  AccelerationStructure bottomLevelAccelerationStructure{};
  AccelerationStructure topLevelAccelerationStructure{};

  Image outputImage{};
  VkExtent2D outputImageExtent{};
  VkImageView outputImageView{VK_NULL_HANDLE};

  Buffer shaderBindingTable{};

  VkDeviceSize raygenBindingOffset{0};
  VkDeviceSize missBindingOffset{0};
  VkDeviceSize missBindingStride{0};
  VkDeviceSize hitBindingOffset{0};
  VkDeviceSize hitBindingStride{0};

  absl::InlinedVector<VkFence, 4> traceCompleteFences{};

  glm::mat4 modelMatrix{1.f};
}; // struct Traceable

} // namespace iris::Renderer::Component

#endif //HEV_IRIS_COMPONENTS_TRACEABLE_H_
