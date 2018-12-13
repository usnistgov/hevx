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

  Mesh mesh;

  IRIS_LOG_LEAVE();
  return std::move(mesh);
} // iris::Renderer::Mesh::Create

