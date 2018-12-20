#ifndef HEV_IRIS_RENDERER_MESH_H_
#define HEV_IRIS_RENDERER_MESH_H_

#include "glm/glm.hpp"
#include "renderer/buffer.h"
#include "renderer/descriptor_sets.h"
#include "renderer/pipeline.h"
#include <vector>

namespace iris::Renderer {

struct MeshData {
  struct Vertex {
    glm::vec3 position{0.0f, 0.0f, 0.0f};
    glm::vec3 normal{0.0f, 0.0f, 0.0f};
    glm::vec4 tangent{0.0f, 0.0f, 0.0f, 0.0f};
    glm::vec2 texcoord{0.0f, 0.0f};
  };

  std::string name{};
  glm::mat4x4 matrix{1.f};

  std::vector<Vertex> vertices{};
  std::vector<unsigned int> indices{};

  VkPrimitiveTopology topology{VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
  std::vector<VkVertexInputBindingDescription> bindingDescriptions{};
  std::vector<VkVertexInputAttributeDescription> attributeDescriptions{};

  void GenerateNormals();
  bool GenerateTangents();
}; // struct MeshData

struct Mesh {
  static constexpr std::size_t const kNumDescriptorSets = 1;

  static tl::expected<Mesh, std::system_error>
  Create(MeshData const& data) noexcept;

  struct ModelBufferData {
    glm::mat4 modelMatrix;
    glm::mat4 modelMatrixInverse;
  };

  struct MaterialBufferData {
    glm::vec2 metallicRoughnessValues;
    glm::vec2 pad0;
    glm::vec4 baseColorFactor;

    //float NormalScale; // optional
    //glm::vec3 EmissiveFactor; // optional
    //float OcclusionStrength; // optional
  };

  glm::mat4 modelMatrix{1.f};
  glm::mat4 modelMatrixInverse{1.f};
  Buffer modelBuffer{};
  Buffer materialBuffer{};
  DescriptorSets descriptorSets;
  Pipeline pipeline{};
  Buffer vertexBuffer{};
  Buffer indexBuffer{};
  std::uint32_t numVertices{0};
  std::uint32_t numIndices{0};

  Mesh()
    : descriptorSets(kNumDescriptorSets) {}

  Mesh(Mesh const&) = delete;
  Mesh(Mesh&& other) = default;
  Mesh& operator=(Mesh const&) = delete;
  Mesh& operator=(Mesh&& other) = default;
  ~Mesh() noexcept = default;
}; // struct Mesh

} // namespace iris::Renderer

#endif // HEV_IRIS_RENDERER_MESH_H_

