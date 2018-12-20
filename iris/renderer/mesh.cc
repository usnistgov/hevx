#include "renderer/mesh.h"
#include "logging.h"
#include "renderer/mikktspace.h"

namespace iris::Renderer {

static int GetNumFaces(SMikkTSpaceContext const* pContext) {
  auto data = reinterpret_cast<MeshData*>(pContext->m_pUserData);

  if (data->indices.empty()) {
    return data->vertices.size() / 3;
  } else {
    return data->indices.size() / 3;
  }
} // GetNumFaces

static int GetNumVerticesOfFace(SMikkTSpaceContext const*, int const) {
  return 3;
} // GetNumVerticesOfFace

static void GetPosition(SMikkTSpaceContext const* pContext, float fvPosOut[],
                        int const iFace, int const iVert) {
  auto data = reinterpret_cast<MeshData*>(pContext->m_pUserData);

  glm::vec3 p;
  if (data->indices.empty()) {
    p = data->vertices[iFace * 3 + iVert].position;
  } else {
    p = data->vertices[data->indices[iFace * 3 + iVert]].position;
  }

  fvPosOut[0] = p.x;
  fvPosOut[1] = p.y;
  fvPosOut[2] = p.z;
} // GetPosition

static void GetNormal(SMikkTSpaceContext const* pContext, float fvNormOut[],
                      const int iFace, const int iVert) {
  auto data = reinterpret_cast<MeshData*>(pContext->m_pUserData);

  glm::vec3 n;
  if (data->indices.empty()) {
    n = data->vertices[iFace * 3 + iVert].position;
  } else {
    n = data->vertices[data->indices[iFace * 3 + iVert]].position;
  }

  fvNormOut[0] = n.x;
  fvNormOut[1] = n.y;
  fvNormOut[2] = n.z;
} // GetNormal

static void GetTexCoord(SMikkTSpaceContext const* pContext, float fvTexcOut[],
                        const int iFace, const int iVert) {
  auto data = reinterpret_cast<MeshData*>(pContext->m_pUserData);

  glm::vec2 c;
  if (data->indices.empty()) {
    c = data->vertices[iFace * 3 + iVert].texcoord;
  } else {
    c = data->vertices[data->indices[iFace * 3 + iVert]].texcoord;
  }

  fvTexcOut[0] = c.x;
  fvTexcOut[1] = c.y;
} // GetTexCoord

static void SetTSpaceBasic(SMikkTSpaceContext const* pContext,
                           float const fvTangent[], float const fSign,
                           int const iFace, int const iVert) {
  auto data = reinterpret_cast<MeshData*>(pContext->m_pUserData);

  glm::vec4 t{fvTangent[0], fvTangent[1], fvTangent[2], fSign};

  if (data->indices.empty()) {
    data->vertices[iFace * 3 + iVert].tangent = t;
  } else {
    data->vertices[data->indices[iFace * 3 + iVert]].tangent = t;
  }
} // SetTSpaceBasic

} // namespace iris::Renderer

void iris::Renderer::MeshData::GenerateNormals() {
  if (indices.empty()) {
    std::size_t const num = vertices.size();
    for (std::size_t i = 0; i < num; i += 3) {
      auto&& a = vertices[i].position;
      auto&& b = vertices[i + 1].position;
      auto&& c = vertices[i + 2].position;
      auto const n = glm::normalize(glm::cross(b - a, c - a));
      vertices[i].normal = n;
      vertices[i + 1].normal = n;
      vertices[i + 2].normal = n;
    }
  } else {
    std::size_t const num = indices.size();
    for (std::size_t i = 0; i < num; i += 3) {
      auto&& a = vertices[indices[i]].position;
      auto&& b = vertices[indices[i + 1]].position;
      auto&& c = vertices[indices[i + 2]].position;
      auto const n = glm::normalize(glm::cross(b - a, c - a));
      vertices[indices[i]].normal = n;
      vertices[indices[i + 1]].normal = n;
      vertices[indices[i + 2]].normal = n;
    }
  }
} // iris::Renderer::MeshData::GenerateNormals

bool iris::Renderer::MeshData::GenerateTangents() {
  std::unique_ptr<SMikkTSpaceInterface> ifc(new SMikkTSpaceInterface);
  ifc->m_getNumFaces = &GetNumFaces;
  ifc->m_getNumVerticesOfFace = &GetNumVerticesOfFace;
  ifc->m_getPosition = &GetPosition;
  ifc->m_getNormal = &GetNormal;
  ifc->m_getTexCoord = &GetTexCoord;
  ifc->m_setTSpaceBasic = &SetTSpaceBasic;
  ifc->m_setTSpace = nullptr;

  std::unique_ptr<SMikkTSpaceContext> ctx(new SMikkTSpaceContext);
  ctx->m_pInterface = ifc.get();
  ctx->m_pUserData = this;

  return genTangSpaceDefault(ctx.get());
} // iris::Renderer::MeshData::GenerateTangents

tl::expected<iris::Renderer::Mesh, std::system_error>
iris::Renderer::Mesh::Create(MeshData const& data) noexcept {
  IRIS_LOG_ENTER();
  Expects(!data.bindingDescriptions.empty());

  bool const hasTexCoords = (data.attributeDescriptions.size() == 4);
  Mesh mesh;
  mesh.modelMatrix = data.matrix;

  if (auto b = Buffer::Create(
        sizeof(ModelBufferData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VMA_MEMORY_USAGE_CPU_TO_GPU, data.name + ":modelBuffer")) {
    mesh.modelBuffer = std::move(*b);
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(b.error());
  }

  if (auto p = mesh.modelBuffer.Map<ModelBufferData*>()) {
    (*p)->modelMatrix = data.matrix;
    (*p)->modelMatrixInverse = glm::inverse(data.matrix);
    mesh.modelBuffer.Unmap();
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(p.error());
  }

  if (auto b = Buffer::Create(
        sizeof(MaterialBufferData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VMA_MEMORY_USAGE_CPU_TO_GPU, data.name + ":materialBuffer")) {
    mesh.materialBuffer = std::move(*b);
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(b.error());
  }

  if (auto p = mesh.materialBuffer.Map<MaterialBufferData*>()) {
    (*p)->metallicRoughnessValues = glm::vec2(0.f, 1.f);
    (*p)->baseColorFactor = glm::vec4(0.8f, 0.f, 0.f, 1.f);
    mesh.materialBuffer.Unmap();
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(p.error());
  }

  std::vector<std::string> shaderMacros;
  if (hasTexCoords) shaderMacros.push_back("-DHAS_TEXCOORDS");

  absl::FixedArray<Shader> shaders(2);

  if (auto vs = Shader::CreateFromFile(
        "assets/shaders/gltf.vert", VK_SHADER_STAGE_VERTEX_BIT, shaderMacros)) {
    shaders[0] = std::move(*vs);
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(vs.error());
  }

  if (auto fs = Shader::CreateFromFile(
        "assets/shaders/gltf.frag", VK_SHADER_STAGE_FRAGMENT_BIT, shaderMacros)) {
    shaders[1] = std::move(*fs);
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(fs.error());
  }

  absl::FixedArray<VkDescriptorSetLayoutBinding> descriptorSetLayoutBinding(2);
  descriptorSetLayoutBinding[0] = {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                                   VK_SHADER_STAGE_ALL_GRAPHICS, nullptr};
  descriptorSetLayoutBinding[1] = {1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                                   VK_SHADER_STAGE_ALL_GRAPHICS, nullptr};

  if (auto d =
        AllocateDescriptorSets(descriptorSetLayoutBinding, kNumDescriptorSets,
                               data.name + ":descriptorSet")) {
    mesh.descriptorSets = std::move(*d);
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(d.error());
  }

  absl::FixedArray<VkWriteDescriptorSet> writeDescriptorSets(2);

  absl::FixedArray<VkPushConstantRange> pushConstantRanges(1);
  pushConstantRanges[0] = {VK_SHADER_STAGE_VERTEX_BIT, 0,
                           sizeof(glm::mat4) * 2 + sizeof(glm::mat3)};

  VkDescriptorBufferInfo modelBufferInfo;
  modelBufferInfo.buffer = mesh.modelBuffer;
  modelBufferInfo.offset = 0;
  modelBufferInfo.range = VK_WHOLE_SIZE;

  VkDescriptorBufferInfo materialBufferInfo;
  materialBufferInfo.buffer = mesh.materialBuffer;
  materialBufferInfo.offset = 0;
  materialBufferInfo.range = VK_WHOLE_SIZE;

  writeDescriptorSets[0] = {
    VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
    nullptr,                           // pNext
    mesh.descriptorSets.sets[0],       // dstSet
    0,                                 // dstBinding
    0,                                 // dstArrayElement
    1,                                 // descriptorCount
    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, // descriptorType
    nullptr,                           // pImageInfo
    &modelBufferInfo,                  // pBufferInfo
    nullptr                            // pTexelBufferView
  };

  writeDescriptorSets[1] = {
    VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
    nullptr,                           // pNext
    mesh.descriptorSets.sets[0],       // dstSet
    1,                                 // dstBinding
    0,                                 // dstArrayElement
    1,                                 // descriptorCount
    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, // descriptorType
    nullptr,                           // pImageInfo
    &materialBufferInfo,               // pBufferInfo
    nullptr                            // pTexelBufferView
  };

  UpdateDescriptorSets(writeDescriptorSets);

  VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI = {};
  inputAssemblyStateCI.sType =
    VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  inputAssemblyStateCI.topology = data.topology;

  VkPipelineViewportStateCreateInfo viewportStateCI = {};
  viewportStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewportStateCI.viewportCount = 1;
  viewportStateCI.scissorCount = 1;

  VkPipelineRasterizationStateCreateInfo rasterizationStateCI = {};
  rasterizationStateCI.sType =
    VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterizationStateCI.polygonMode = VK_POLYGON_MODE_FILL;
  rasterizationStateCI.cullMode = VK_CULL_MODE_BACK_BIT;
  if (glm::determinant(data.matrix) < 0.f) {
    rasterizationStateCI.frontFace = VK_FRONT_FACE_CLOCKWISE;
  } else {
    rasterizationStateCI.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  }
  rasterizationStateCI.lineWidth = 1.f;

  VkPipelineMultisampleStateCreateInfo multisampleStateCI = {};
  multisampleStateCI.sType =
    VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisampleStateCI.rasterizationSamples = sSurfaceSampleCount;
  multisampleStateCI.minSampleShading = 1.f;

  VkPipelineDepthStencilStateCreateInfo depthStencilStateCI = {};
  depthStencilStateCI.sType =
    VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depthStencilStateCI.depthTestEnable = VK_TRUE;
  depthStencilStateCI.depthWriteEnable = VK_TRUE;
  depthStencilStateCI.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

  absl::FixedArray<VkPipelineColorBlendAttachmentState>
    colorBlendAttachmentStates(1);
  colorBlendAttachmentStates[0] = {
    VK_FALSE,                            // blendEnable
    VK_BLEND_FACTOR_SRC_ALPHA,           // srcColorBlendFactor
    VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, // dstColorBlendFactor
    VK_BLEND_OP_ADD,                     // colorBlendOp
    VK_BLEND_FACTOR_ONE,                 // srcAlphaBlendFactor
    VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, // dstAlphaBlendFactor
    VK_BLEND_OP_ADD,                     // alphaBlendOp
    VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT // colorWriteMask
  };

  absl::FixedArray<VkDynamicState> dynamicStates{VK_DYNAMIC_STATE_VIEWPORT,
                                                 VK_DYNAMIC_STATE_SCISSOR};

  absl::FixedArray<VkDescriptorSetLayout> descriptorSetLayouts(2);
  descriptorSetLayouts[0] = sBaseDescriptorSetLayout;
  descriptorSetLayouts[1] = mesh.descriptorSets.layout;

  if (auto p = Pipeline::CreateGraphics(
        descriptorSetLayouts, pushConstantRanges, shaders,
        data.bindingDescriptions, data.attributeDescriptions,
        inputAssemblyStateCI, viewportStateCI, rasterizationStateCI,
        multisampleStateCI, depthStencilStateCI, colorBlendAttachmentStates,
        dynamicStates, 0, data.name + ":pipeline")) {
    mesh.pipeline = std::move(*p);
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(p.error());
  }

  if (!data.indices.empty()) {
    mesh.numIndices = static_cast<std::uint32_t>(data.indices.size());

    if (auto ib = Buffer::CreateFromMemory(
          data.indices.size() * sizeof(decltype(data.indices)::value_type),
          VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY,
          gsl::not_null(data.indices.data()), data.name + ":indexBuffer")) {
      mesh.indexBuffer = std::move(*ib);
    } else {
      IRIS_LOG_LEAVE();
      return tl::unexpected(ib.error());
    }
  }

  std::size_t const stride = (3 + 3 + 4 + (hasTexCoords ? 2 : 0));
  std::vector<float> vertexBuffer(data.vertices.size() * stride);
  for (std::size_t i = 0; i < data.vertices.size(); ++i) {
    auto&& vertex = data.vertices[i];
    std::size_t const offset = i * stride;
    vertexBuffer[offset + 0] = vertex.position.x;
    vertexBuffer[offset + 1] = vertex.position.y;
    vertexBuffer[offset + 2] = vertex.position.z;
    vertexBuffer[offset + 3] = vertex.normal.x;
    vertexBuffer[offset + 4] = vertex.normal.y;
    vertexBuffer[offset + 5] = vertex.normal.z;
    vertexBuffer[offset + 6] = vertex.tangent.x;
    vertexBuffer[offset + 7] = vertex.tangent.y;
    vertexBuffer[offset + 8] = vertex.tangent.z;
    vertexBuffer[offset + 9] = vertex.tangent.w;
    if (hasTexCoords) {
      vertexBuffer[offset + 10] = vertex.texcoord.x;
      vertexBuffer[offset + 11] = vertex.texcoord.y;
    }
  }

  if (auto vb = Buffer::CreateFromMemory(
        vertexBuffer.size() * sizeof(float), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY, gsl::not_null(vertexBuffer.data()),
        data.name + ":vertexBuffer")) {}

  if (auto vb = Buffer::CreateFromMemory(
        data.vertices.size() * sizeof(decltype(data.vertices)::value_type),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_GPU_ONLY,
        gsl::not_null(data.vertices.data()), data.name + ":vertexBuffer")) {
    mesh.vertexBuffer = std::move(*vb);
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(vb.error());
  }

  IRIS_LOG_LEAVE();
  return std::move(mesh);
} // iris::Renderer::Mesh::Create

