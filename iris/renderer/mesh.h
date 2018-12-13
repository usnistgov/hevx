#ifndef HEV_IRIS_RENDERER_MESH_H_
#define HEV_IRIS_RENDERER_MESH_H_

#include "glm/glm.hpp"
#include "renderer/buffer.h"
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

  glm::mat4x4 matrix{1.f};
  std::vector<Vertex> vertices;
  std::vector<unsigned int> indices;

  VkPrimitiveTopology topology;
  std::vector<VkVertexInputBindingDescription> bindingDescriptions;
  std::vector<VkVertexInputAttributeDescription> attributeDescriptions;

  void GenerateNormals();
  bool GenerateTangents();
}; // struct MeshData

struct Mesh {
  static tl::expected<Mesh, std::system_error>
  Create(MeshData const& data) noexcept;

  Buffer vertexBuffer{};
  Buffer indexBuffer{};
  Pipeline pipeline{};

  Mesh() = default;
  Mesh(Mesh const&) = delete;
  Mesh(Mesh&& other) = default;
  Mesh& operator=(Mesh const&) = delete;
  Mesh& operator=(Mesh&& other) = default;
  ~Mesh() noexcept = default;
}; // struct Mesh

} // namespace iris::Renderer

#endif // HEV_IRIS_RENDERER_MESH_H_

