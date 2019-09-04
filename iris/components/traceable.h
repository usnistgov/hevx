#ifndef HEV_IRIS_COMPONENTS_TRACEABLE_H_
#define HEV_IRIS_COMPONENTS_TRACEABLE_H_

#include "absl/container/inlined_vector.h"
#include "glm/mat4x4.hpp"
#include "glm/vec4.hpp"
#include "iris/acceleration_structure.h"
#include "iris/buffer.h"
#include "iris/image.h"
#include "iris/pipeline.h"
#include "iris/shader.h"
#include "iris/vulkan.h"
#include <cstdint>

namespace iris::Renderer::Component {

struct Traceable {
  VkDescriptorSetLayout descriptorSetLayout{VK_NULL_HANDLE};
  VkDescriptorSet descriptorSet{VK_NULL_HANDLE};
  absl::InlinedVector<ShaderGroup, 8> shaderGroups;
  Pipeline pipeline{};

  Buffer raygenShaderBindingTable{};
  Buffer missShadersBindingTable{};
  Buffer hitShadersBindingTable{};

  VkDeviceSize missBindingOffset{0};
  VkDeviceSize missBindingStride{0};
  VkDeviceSize hitBindingOffset{0};
  VkDeviceSize hitBindingStride{0};

  struct Geometry {
    Buffer buffer{};
    VkGeometryNV geometry{};
    bool bottomLevelDirty{true};
    AccelerationStructure bottomLevelAccelerationStructure{};
  };

  absl::InlinedVector<Geometry, 128> geometries;
  bool topLevelDirty{true};
  AccelerationStructure topLevelAccelerationStructure{};

  VkExtent2D outputImageExtent{1600, 1200};
  Image outputImage{};
  VkImageView outputImageView{};

  VkFence traceFinishedFence{VK_NULL_HANDLE};
}; // struct Traceable

} // namespace iris::Renderer::Component

#endif //HEV_IRIS_COMPONENTS_TRACEABLE_H_
