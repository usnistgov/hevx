#ifndef HEV_IRIS_COMPONENTS_RENDERABLE_H_
#define HEV_IRIS_COMPONENTS_RENDERABLE_H_

#include "absl/container/inlined_vector.h"
#include "glm/mat4x4.hpp"
#include "glm/vec4.hpp"
#include "iris/buffer.h"
#include "iris/renderer.h"
#include "iris/vulkan.h"
#include <cstdint>

namespace iris::Renderer::Component {

struct Renderable {
  MaterialID material{};

  static constexpr std::size_t const kMaxBuffers = 4;
  absl::InlinedVector<Buffer, kMaxBuffers> buffers{};

  Buffer vertexBuffer{};
  VkDeviceSize vertexBufferBindingOffset{0};

  Buffer indexBuffer{};
  VkDeviceSize indexBufferBindingOffset{0};

  glm::mat4 modelMatrix{1.f};

  VkIndexType indexType{VK_INDEX_TYPE_UINT32};
  std::uint32_t numIndices{0};
  std::uint32_t instanceCount{1};
  std::uint32_t firstIndex{0};
  std::uint32_t vertexOffset{0};
  std::uint32_t firstInstance{0};

  std::uint32_t numVertices{0};
  std::uint32_t firstVertex{0};

  glm::vec4 boundingSphere{};
}; // struct Renderable

} // namespace iris::Renderer::Component

#endif // HEV_IRIS_COMPONENTS_RENDERABLE_H_
