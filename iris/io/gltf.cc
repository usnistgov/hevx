#include "io/gltf.h"

#include "config.h"

#include "absl/container/fixed_array.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/inlined_vector.h"
#include "components/renderable.h"
#include "components/traceable.h"
#include "error.h"
#include "fmt/format.h"
#include "glm/glm.hpp"
#include "glm/gtc/quaternion.hpp"
#include "glm/gtc/type_ptr.hpp"
#include "gsl/gsl"
#include "io/read_file.h"
#include "io/shadertoy.h"
#include "logging.h"
#include "mikktspace.h"
#if PLATFORM_COMPILER_GCC
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#endif
#include "Miniball.hpp"
#if PLATFORM_COMPILER_GCC
#pragma GCC diagnostic pop
#endif
#include "renderer.h"
#include "renderer_private.h"
#include "stb_image.h"
#include "stb_image_resize.h"
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace nlohmann {

template <>
struct adl_serializer<glm::vec3> {
  static void to_json(json& j, glm::vec3 const& vec) {
    j = std::vector<float>{vec.x, vec.y, vec.z};
  }

  static void from_json(json const& j, glm::vec3& vec) {
    auto arr = j.get<std::vector<float>>();
    if (arr.size() != 3) throw std::runtime_error("wrong number of elements");
    vec.x = arr[0];
    vec.y = arr[1];
    vec.z = arr[2];
  }
}; // struct adl_serializer<glm::vec3>

template <>
struct adl_serializer<glm::vec4> {
  static void to_json(json& j, glm::vec4 const& vec) {
    j = std::vector<float>{vec.x, vec.y, vec.z, vec.w};
  }

  static void from_json(json const& j, glm::vec4& vec) {
    auto arr = j.get<std::vector<float>>();
    if (arr.size() != 4) throw std::runtime_error("wrong number of elements");
    vec.x = arr[0];
    vec.y = arr[1];
    vec.z = arr[2];
    vec.w = arr[3];
  }
}; // struct adl_serializer<glm::vec4>

template <>
struct adl_serializer<glm::quat> {
  static void to_json(json& j, glm::quat const& q) {
    j = std::vector<float>{q.w, q.x, q.y, q.z};
  }

  static void from_json(json const& j, glm::quat& q) {
    auto arr = j.get<std::vector<float>>();
    if (arr.size() != 4) throw std::runtime_error("wrong number of elements");
    q.w = arr[0];
    q.x = arr[1];
    q.y = arr[2];
    q.z = arr[3];
  }
}; // struct adl_serializer<glm::vec4>

template <>
struct adl_serializer<glm::mat4> {
  static void to_json(json& j, glm::mat4 const& mat) {
    j = std::vector<float>(glm::value_ptr(mat), glm::value_ptr(mat) + 16);
  }

  static void from_json(json const& j, glm::mat4x4& mat) {
    auto arr = j.get<std::vector<float>>();
    if (arr.size() != 16) throw std::runtime_error("wrong number of elements");
    std::copy_n(arr.begin(), 16, glm::value_ptr(mat));
  }
}; // struct adl_serializer<glm::vec4>

} // namespace nlohmann

namespace iris::gltf {

using json = nlohmann::json;

struct MaterialBuffer {
  glm::vec4 MetallicRoughnessNormalOcclusion;
  glm::vec4 BaseColorFactor;
  glm::vec3 EmissiveFactor;
}; // struct MaterialBuffer

struct Asset {
  std::optional<std::string> copyright;
  std::optional<std::string> generator;
  std::string version;
  std::optional<std::string> minVersion;
}; // struct Asset

void to_json(json& j, Asset const& asset) {
  j = json{{"version", asset.version}};
  if (asset.copyright) j["copyright"] = *asset.copyright;
  if (asset.generator) j["generator"] = *asset.generator;
  if (asset.minVersion) j["minVersion"] = *asset.minVersion;
} // to_json

void from_json(json const& j, Asset& asset) {
  j.at("version").get_to(asset.version);
  if (j.find("copyright") != j.end()) asset.copyright = j["copyright"];
  if (j.find("generator") != j.end()) asset.generator = j["generator"];
  if (j.find("minVersion") != j.end()) asset.minVersion = j["minVersion"];
}

struct Accessor {
  std::optional<int> bufferView; // index into gltf.bufferViews
  std::optional<int> byteOffset;
  int componentType;
  std::optional<bool> normalized;
  int count;
  std::string type;
  std::optional<std::vector<double>> min;
  std::optional<std::vector<double>> max;
  std::optional<std::string> name;
}; // struct Accessor

void to_json(json& j, Accessor const& accessor) {
  j = json{{"componentType", accessor.componentType},
           {"count", accessor.count},
           {"type", accessor.type}};

  if (accessor.bufferView) j["bufferView"] = *accessor.bufferView;
  if (accessor.byteOffset) j["byteOffset"] = *accessor.byteOffset;
  if (accessor.normalized) j["normalized"] = *accessor.normalized;
  if (accessor.min) j["min"] = *accessor.min;
  if (accessor.max) j["max"] = *accessor.max;
  if (accessor.name) j["name"] = *accessor.name;
}

void from_json(json const& j, Accessor& accessor) {
  j.at("componentType").get_to(accessor.componentType);
  j.at("count").get_to(accessor.count);
  j.at("type").get_to(accessor.type);

  if (j.find("bufferView") != j.end()) accessor.bufferView = j["bufferView"];
  if (j.find("byteOffset") != j.end()) accessor.byteOffset = j["byteOffset"];
  if (j.find("normalized") != j.end()) accessor.normalized = j["normalized"];
  if (j.find("min") != j.end()) {
    accessor.min = j["min"].get<decltype(Accessor::min)::value_type>();
  }
  if (j.find("max") != j.end()) {
    accessor.max = j["max"].get<decltype(Accessor::max)::value_type>();
  }
  if (j.find("name") != j.end()) accessor.name = j["name"];
}

struct Buffer {
  int byteLength;
  std::optional<std::string> uri;
  std::optional<std::string> name;
}; // struct Buffer

void to_json(json& j, Buffer const& buffer) {
  j = json{{"byteLength", buffer.byteLength}};
  if (buffer.uri) j["uri"] = *buffer.uri;
  if (buffer.name) j["name"] = *buffer.name;
}

void from_json(json const& j, Buffer& buffer) {
  buffer.byteLength = j.at("byteLength");
  if (j.find("uri") != j.end()) buffer.uri = j["uri"];
  if (j.find("name") != j.end()) buffer.name = j["name"];
}

struct BufferView {
  int buffer; // index into gltf.buffers
  std::optional<int> byteOffset;
  int byteLength;
  std::optional<int> byteStride;
  std::optional<int> target;
  std::optional<std::string> name;
}; // struct BufferView

void to_json(json& j, BufferView const& view) {
  j = json{{"buffer", view.buffer}, {"byteLength", view.byteLength}};

  if (view.byteOffset) j["byteOffset"] = *view.byteOffset;
  if (view.byteStride) j["byteStride"] = *view.byteStride;
  if (view.target) j["target"] = *view.target;
  if (view.name) j["name"] = *view.name;
}

void from_json(json const& j, BufferView& view) {
  view.buffer = j.at("buffer");
  view.byteLength = j.at("byteLength");

  if (j.find("byteOffset") != j.end()) view.byteOffset = j["byteOffset"];
  if (j.find("byteStride") != j.end()) view.byteStride = j["byteStride"];
  if (j.find("target") != j.end()) view.target = j["target"];
  if (j.find("name") != j.end()) view.name = j["name"];
}

struct Image {
  std::optional<std::string> uri;
  std::optional<std::string> mimeType;
  std::optional<int> bufferView; // index into gltf.bufferViews
  std::optional<std::string> name;
}; // struct Image

void to_json(json& j, Image const& image) {
  j = json{};
  if (image.uri) j["uri"] = *image.uri;
  if (image.mimeType) j["mimeType"] = *image.mimeType;
  if (image.bufferView) j["bufferView"] = *image.bufferView;
  if (image.name) j["name"] = *image.name;
}

void from_json(json const& j, Image& image) {
  if (j.find("uri") != j.end()) image.uri = j["uri"];
  if (j.find("mimeType") != j.end()) image.mimeType = j["mimeType"];
  if (j.find("bufferView") != j.end()) image.bufferView = j["bufferView"];
  if (j.find("name") != j.end()) image.name = j["name"];
}

struct TextureInfo {
  int index; // index into gltf.textures
  std::optional<int> texCoord;
}; // struct TextureInfo

void to_json(json& j, TextureInfo const& info) {
  j = json{{"index", info.index}};
  if (info.texCoord) j["texCoord"] = *info.texCoord;
}

void from_json(json const& j, TextureInfo& info) {
  info.index = j.at("index");
  if (j.find("texCoord") != j.end()) info.texCoord = j["texCoord"];
}

struct PBRMetallicRoughness {
  std::optional<glm::vec4> baseColorFactor;
  std::optional<TextureInfo> baseColorTexture;
  std::optional<double> metallicFactor;
  std::optional<double> roughnessFactor;
  std::optional<TextureInfo> metallicRoughnessTexture;
}; // struct PBRMetallicRoughness

void to_json(json& j, PBRMetallicRoughness pbr) {
  j = json{};

  if (pbr.baseColorFactor) j["baseColorFactor"] = *pbr.baseColorFactor;
  if (pbr.baseColorTexture) j["baseColorTexture"] = *pbr.baseColorTexture;
  if (pbr.metallicFactor) j["metallicFactor"] = *pbr.metallicFactor;
  if (pbr.roughnessFactor) j["roughnessFactor"] = *pbr.roughnessFactor;
  if (pbr.metallicRoughnessTexture) {
    j["metallicRoughnessTexture"] = *pbr.metallicRoughnessTexture;
  }
}

void from_json(json const& j, PBRMetallicRoughness& pbr) {
  if (j.find("baseColorFactor") != j.end()) {
    pbr.baseColorFactor = j["baseColorFactor"].get<glm::vec4>();
  }
  if (j.find("baseColorTexture") != j.end()) {
    pbr.baseColorTexture = j["baseColorTexture"]; //.get<TextureInfo>();
  }
  if (j.find("metallicFactor") != j.end()) {
    pbr.metallicFactor = j["metallicFactor"];
  }
  if (j.find("roughnessFactor") != j.end()) {
    pbr.roughnessFactor = j["roughnessFactor"];
  }
  if (j.find("metallicRoughnessTexture") != j.end()) {
    pbr.metallicRoughnessTexture = j["metallicRoughnessTexture"];
  }
}

struct NormalTextureInfo {
  int index; // index into gltf.textures
  std::optional<int> texCoord;
  std::optional<double> scale;
}; // struct NormalTextureInfo

void to_json(json& j, NormalTextureInfo const& info) {
  j = json{{"index", info.index}};
  if (info.texCoord) j["texCoord"] = *info.texCoord;
  if (info.scale) j["scale"] = *info.scale;
}

void from_json(json const& j, NormalTextureInfo& info) {
  info.index = j.at("index");
  if (j.find("texCoord") != j.end()) info.texCoord = j["texCoord"];
  if (j.find("scale") != j.end()) info.scale = j["scale"];
}

struct OcclusionTextureInfo {
  int index; // index into gltf.textures
  std::optional<int> texCoord;
  std::optional<double> strength;
}; // struct OcclusionTextureInfo

void to_json(json& j, OcclusionTextureInfo const& info) {
  j = json{{"index", info.index}};
  if (info.texCoord) j["texCoord"] = *info.texCoord;
  if (info.strength) j["strength"] = *info.strength;
}

void from_json(json const& j, OcclusionTextureInfo& info) {
  info.index = j.at("index");
  if (j.find("texCoord") != j.end()) info.texCoord = j["texCoord"];
  if (j.find("strength") != j.end()) info.strength = j["strength"];
}

struct NISTTechniquesRaytracingMaterialExtension {
  int hitShaders;
  std::optional<json> shaderRecord;
}; // struct NISTTechniquesRaytracingMaterialExtension

void to_json(json& j, NISTTechniquesRaytracingMaterialExtension const& ext) {
  j = json{{"hitShaders",  ext.hitShaders}};
  if (ext.shaderRecord) j["shaderRecord"] = *ext.shaderRecord;
}

void from_json(json const& j, NISTTechniquesRaytracingMaterialExtension& ext) {
  ext.hitShaders = j.at("hitShaders");
  if (j.find("shaderRecord") != j.end()) ext.shaderRecord = j["shaderRecord"];
}

struct Material {
  std::optional<std::string> name;
  std::optional<PBRMetallicRoughness> pbrMetallicRoughness;
  std::optional<NormalTextureInfo> normalTexture;
  std::optional<OcclusionTextureInfo> occlusionTexture;
  std::optional<TextureInfo> emissiveTexture;
  std::optional<glm::vec3> emissiveFactor;
  std::optional<std::string> alphaMode;
  std::optional<double> alphaCutoff;
  std::optional<bool> doubleSided;
  std::optional<NISTTechniquesRaytracingMaterialExtension>
    nistTechniquesRaytracingExtension;
}; // struct Material

void to_json(json& j, Material const& m) {
  j = json{};
  if (m.name) j["name"] = *m.name;
  if (m.pbrMetallicRoughness) {
    j["pbrMetallicRoughness"] = *m.pbrMetallicRoughness;
  }
  if (m.normalTexture) j["normalTexture"] = *m.normalTexture;
  if (m.occlusionTexture) j["occlusionTexture"] = *m.occlusionTexture;
  if (m.emissiveTexture) j["emissiveTexture"] = *m.emissiveTexture;
  if (m.emissiveFactor) j["emissiveFactor"] = *m.emissiveFactor;
  if (m.alphaMode) j["alphaMode"] = *m.alphaMode;
  if (m.alphaCutoff) j["alphaCutoff"] = *m.alphaCutoff;
  if (m.doubleSided) j["doubleSided"] = *m.doubleSided;
  if (m.nistTechniquesRaytracingExtension) {
    json e{};
    e["NIST_techniques_raytracing"] = *m.nistTechniquesRaytracingExtension;
    j["extensions"] = e;
  }
}

void from_json(json const& j, Material& m) {
  if (j.find("name") != j.end()) m.name = j["name"];
  if (j.find("pbrMetallicRoughness") != j.end()) {
    m.pbrMetallicRoughness = j["pbrMetallicRoughness"];
  }
  if (j.find("normalTexture") != j.end()) m.normalTexture = j["normalTexture"];
  if (j.find("occlusionTexture") != j.end()) {
    m.occlusionTexture = j["occlusionTexture"];
  }
  if (j.find("emissiveTexture") != j.end()) {
    m.emissiveTexture = j["emissiveTexture"];
  }
  if (j.find("emissiveFactor") != j.end()) {
    m.emissiveFactor = j["emissiveFactor"].get<glm::vec3>();
  }
  if (j.find("alphaMode") != j.end()) m.alphaMode = j["alphaMode"];
  if (j.find("alphaCutoff") != j.end()) m.alphaCutoff = j["alphaCutoff"];
  if (j.find("doubleSided") != j.end()) m.doubleSided = j["doubleSided"];
  if (j.find("extensions") != j.end()) {
    auto&& e = j["extensions"];
    if (e.find("NIST_techniques_raytracing") != e.end()) {
      m.nistTechniquesRaytracingExtension = e["NIST_techniques_raytracing"];
    }
  }
}

struct Primitive {
  std::map<std::string, int> attributes; // index into gltf.accessors
  std::optional<int> indices;            // index into gltf.accessors
  std::optional<int> material;           // index into gltf.materials
  std::optional<int> mode;
  std::optional<std::vector<int>> targets;
}; // struct Primitive

void to_json(json& j, Primitive const& prim) {
  j = json{{"attributes", prim.attributes}};
  if (prim.indices) j["indices"] = *prim.indices;
  if (prim.material) j["material"] = *prim.material;
  if (prim.mode) j["mode"] = *prim.mode;
  if (prim.targets) j["targets"] = *prim.targets;
}

void from_json(json const& j, Primitive& prim) {
  prim.attributes = j.at("attributes").get<decltype(Primitive::attributes)>();
  if (j.find("indices") != j.end()) prim.indices = j["indices"];
  if (j.find("material") != j.end()) prim.material = j["material"];
  if (j.find("mode") != j.end()) prim.mode = j["mode"];
  if (j.find("targets") != j.end()) {
    prim.targets = j["targets"].get<decltype(Primitive::targets)::value_type>();
  }
}

struct Mesh {
  std::vector<Primitive> primitives;
  std::optional<std::string> name;
}; // struct Mesh

void to_json(json& j, Mesh const& mesh) {
  j = json{{"primitives", mesh.primitives}};
  if (mesh.name) j["name"] = *mesh.name;
}

void from_json(json const& j, Mesh& mesh) {
  mesh.primitives = j.at("primitives").get<decltype(Mesh::primitives)>();
  if (j.find("name") != j.end()) mesh.name = j["name"];
}

struct NodeShaderToy {
  std::optional<std::string> url;
  std::optional<std::string> code;
}; // struct NodeShaderToy

void to_json(json& j, NodeShaderToy const& m) {
  j = json{};
  if (m.url) j["url"] = *m.url;
  if (m.code) j["code"] = *m.code;
}

void from_json(json const& j, NodeShaderToy& m) {
  if (j.find("url") != j.end()) m.url = j["url"];
  if (j.find("code") != j.end()) m.code = j["code"];
}

struct Node {
  std::optional<std::vector<int>> children; // indices into gltf.nodes
  std::optional<glm::mat4x4> matrix;
  std::optional<int> mesh; // index into gltf.meshes
  std::optional<glm::quat> rotation;
  std::optional<glm::vec3> scale;
  std::optional<glm::vec3> translation;
  std::optional<std::string> name;
  std::optional<NodeShaderToy> shaderToy;
}; // struct Node

void to_json(json& j, Node const& node) {
  j = json{};
  if (node.children) j["children"] = *node.children;
  if (node.matrix) j["matrix"] = *node.matrix;
  if (node.mesh) j["mesh"] = *node.mesh;
  if (node.rotation) j["rotation"] = *node.rotation;
  if (node.scale) j["scale"] = *node.scale;
  if (node.translation) j["translation"] = *node.translation;
  if (node.name) j["name"] = *node.name;
  if (node.shaderToy) {
    j["extras"] = json{};
    j["extras"]["HEV"] = json{};
    j["extras"]["HEV"]["shadertoy"] = *node.shaderToy;
  }
}

void from_json(json const& j, Node& node) {
  if (j.find("children") != j.end()) {
    node.children = j["children"].get<decltype(Node::children)::value_type>();
  }
  if (j.find("matrix") != j.end()) node.matrix = j["matrix"].get<glm::mat4x4>();
  if (j.find("mesh") != j.end()) node.mesh = j["mesh"];
  if (j.find("rotation") != j.end()) {
    node.rotation = j["rotation"].get<glm::quat>();
  }
  if (j.find("scale") != j.end()) node.scale = j["scale"].get<glm::vec3>();
  if (j.find("translation") != j.end()) {
    node.translation = j["translation"].get<glm::vec3>();
  }
  if (j.find("name") != j.end()) node.name = j["name"];
  if (j.find("extras") != j.end()) {
    auto&& e = j["extras"];
    if (e.find("HEV") != e.end()) {
      auto&& h = e["HEV"];
      if (h.find("shadertoy") != h.end()) { node.shaderToy = h["shadertoy"]; }
    }
  }
}

struct Sampler {
  std::optional<int> magFilter;
  std::optional<int> minFilter;
  std::optional<int> wrapS;
  std::optional<int> wrapT;
  std::optional<std::string> name;
}; // struct Sampler

void to_json(json& j, Sampler const& sampler) {
  j = json{};
  if (sampler.magFilter) j["magFilter"] = *sampler.magFilter;
  if (sampler.minFilter) j["minFilter"] = *sampler.minFilter;
  if (sampler.wrapS) j["wrapS"] = *sampler.wrapS;
  if (sampler.wrapT) j["wrapT"] = *sampler.wrapT;
  if (sampler.name) j["name"] = *sampler.name;
}

void from_json(json const& j, Sampler& sampler) {
  if (j.find("magFilter") != j.end()) sampler.magFilter = j["magFilter"];
  if (j.find("minFilter") != j.end()) sampler.minFilter = j["minFilter"];
  if (j.find("wrapS") != j.end()) sampler.wrapS = j["wrapS"];
  if (j.find("wrapT") != j.end()) sampler.wrapT = j["wrapT"];
  if (j.find("name") != j.end()) sampler.name = j["name"];
}

struct Scene {
  std::optional<std::vector<int>> nodes; // indices into gltf.nodes
  std::optional<std::string> name;
}; // struct Scene

void to_json(json& j, Scene const& scene) {
  j = json{};
  if (scene.nodes) j["nodes"] = *scene.nodes;
  if (scene.name) j["name"] = *scene.name;
}

void from_json(json const& j, Scene& scene) {
  if (j.find("nodes") != j.end()) {
    scene.nodes = j["nodes"].get<decltype(Scene::nodes)::value_type>();
  }
  if (j.find("name") != j.end()) scene.name = j["name"];
}

struct Texture {
  std::optional<int> sampler; // index into gltf.samplers
  std::optional<int> source;  // index into gltf.images
  std::optional<std::string> name;
}; // struct Texture

void to_json(json& j, Texture const& tex) {
  j = json{};
  if (tex.sampler) j["sampler"] = *tex.sampler;
  if (tex.source) j["source"] = *tex.source;
  if (tex.name) j["name"] = *tex.name;
}

void from_json(json const& j, Texture& tex) {
  if (j.find("sampler") != j.end()) tex.sampler = j["sampler"];
  if (j.find("source") != j.end()) tex.source = j["source"];
  if (j.find("name") != j.end()) tex.name = j["name"];
}

struct NISTTechniquesRaytracingExtensionShaderBindingTable {
  int raygenShader;
  int missShader;
  std::vector<std::map<std::string, int>> hitShaders;
}; // struct NISTTechniquesRaytracingExtensionShaderBindingTable

void to_json(json& j,
             NISTTechniquesRaytracingExtensionShaderBindingTable const& sbt) {
  j = json{{"raygenShader", sbt.raygenShader},
           {"missShader", sbt.missShader},
           {"hitShaders", sbt.hitShaders}};
}

void from_json(json const& j,
               NISTTechniquesRaytracingExtensionShaderBindingTable& sbt) {
  sbt.raygenShader = j.at("raygenShader");
  sbt.missShader = j.at("missShader");
  sbt.hitShaders =
    j.at("hitShaders")
      .get<decltype(
        NISTTechniquesRaytracingExtensionShaderBindingTable::hitShaders)>();
}

struct NISTTechniquesRaytracingExtensionShader {
  int type;
  std::string uri;
}; // struct NISTTechniquesRaytracingExtensionShader

void to_json(json& j, NISTTechniquesRaytracingExtensionShader const& shader) {
  j = json{};
  j["type"] = shader.type;
  j["uri"] = shader.uri;
}

void from_json(json const& j, NISTTechniquesRaytracingExtensionShader& shader) {
  shader.type = j.at("type");
  shader.uri = j.at("uri");
}

struct NISTTechniquesRaytracingExtension {
  NISTTechniquesRaytracingExtensionShaderBindingTable shaderBindingTable;
  std::vector<NISTTechniquesRaytracingExtensionShader> shaders;
}; // struct NISTTechniquesRaytracingExtension

void to_json(json& j, NISTTechniquesRaytracingExtension const& ext) {
  j = json{};
  j["shaderBindingTable"] = ext.shaderBindingTable;
  j["shaders"] = ext.shaders;
}

void from_json(json const& j, NISTTechniquesRaytracingExtension& ext) {
  ext.shaderBindingTable = j.at("shaderBindingTable");
  ext.shaders =
    j.at("shaders").get<decltype(NISTTechniquesRaytracingExtension::shaders)>();
}

struct GLTF {
  // TODO: These need to match gltf.frag
  static constexpr std::size_t const kBaseColorBinding = 1;
  static constexpr std::size_t const kNormalBinding = 2;
  static constexpr std::size_t const kMetallicRoughnessBinding = 3;
  static constexpr std::size_t const kEmissiveBinding = 4;
  static constexpr std::size_t const kOcclusionBinding = 5;

  std::optional<std::vector<Accessor>> accessors;
  Asset asset;
  std::optional<std::vector<Buffer>> buffers;
  std::optional<std::vector<BufferView>> bufferViews;
  std::optional<std::vector<Image>> images;
  std::optional<std::vector<Material>> materials;
  std::optional<std::vector<Mesh>> meshes;
  std::optional<std::vector<Node>> nodes;
  std::optional<std::vector<Sampler>> samplers;
  std::optional<int> scene;
  std::optional<std::vector<Scene>> scenes;
  std::optional<std::vector<Texture>> textures;
  std::optional<std::vector<std::string>> extensionsUsed;
  std::optional<std::vector<std::string>> extensionsRequired;
  std::optional<NISTTechniquesRaytracingExtension>
    nistTechniquesRaytracingExtension;

  absl::flat_hash_map<int, Renderer::MaterialID> materialsMap;

  template <typename T>
  expected<std::vector<T>, std::system_error>
  ParseNode(Renderer::CommandQueue commandQueue, int nodeIdx,
            glm::mat4x4 parentMat, std::filesystem::path const& path,
            std::vector<std::vector<std::byte>> const& buffersBytes,
            std::vector<VkExtent2D> const& imagesExtents,
            std::vector<std::vector<std::byte>> imagesBytes);

  expected<
    std::tuple<VkDescriptorSetLayout, VkDescriptorSet,
               absl::InlinedVector<iris::ShaderGroup, 8>, iris::Pipeline>,
    std::system_error>
  ParseRaytracingPipeline(int numGeometries);

  expected<void, std::system_error>
  ParseRaytracingMaterials(gsl::span<ShaderGroup> shaderGroups);

  template <typename T>
  expected<T, std::system_error>
  ParsePrimitive(Renderer::CommandQueue commandQueue,
                 std::string const& meshName, glm::mat4x4 const& nodeMat,
                 std::vector<std::vector<std::byte>> const& buffersBytes,
                 std::vector<VkExtent2D> const& imagesExtents,
                 std::vector<std::vector<std::byte>> const& imagesBytes,
                 Node const& node, Primitive const& primitive);

  struct DeviceTexture {
    iris::Image texture{};
    VkImageView view{VK_NULL_HANDLE};
    VkSampler sampler{VK_NULL_HANDLE};
  };

  expected<DeviceTexture, std::system_error>
  CreateTexture(Renderer::CommandQueue commandQueue, TextureInfo textureInfo,
                std::vector<VkExtent2D> const& imagesExtents,
                std::vector<std::vector<std::byte>> imagesBytes, bool srgb);

  expected<Renderer::Component::Material, std::system_error> CreateMaterial(
    Renderer::CommandQueue commandQueue, std::string const& meshName,
    VkPrimitiveTopology topology, bool hasTexCoords,
    decltype(Renderer::Component::Material::vertexInputBindingDescriptions)
      vertexInputBindingDescriptions,
    decltype(Renderer::Component::Material::vertexInputAttributeDescriptions)
      vertexInputAttributeDescriptions,
    VkFrontFace frontFace, int materialIndex,
    std::vector<VkExtent2D> const& imagesExtents,
    std::vector<std::vector<std::byte>> const& imagesBytes);
}; // struct GLTF

void to_json(json& j, GLTF const& g) {
  j = json{{"asset", g.asset}};
  if (g.accessors) j["accessors"] = *g.accessors;
  if (g.buffers) j["buffers"] = *g.buffers;
  if (g.images) j["image"] = *g.images;
  if (g.materials) j["materials"] = *g.materials;
  if (g.meshes) j["meshes"] = *g.meshes;
  if (g.nodes) j["nodes"] = *g.nodes;
  if (g.samplers) j["samplers"] = *g.samplers;
  if (g.scene) j["scene"] = *g.scene;
  if (g.scenes) j["scenes"] = *g.scenes;
  if (g.textures) j["textures"] = *g.textures;
  if (g.extensionsUsed) j["extensionsUsed"] = *g.extensionsUsed;
  if (g.extensionsRequired) j["extensionsRequired"] = *g.extensionsRequired;
  if (g.nistTechniquesRaytracingExtension) {
    json e{};
    e["NIST_techniques_raytracing"] = *g.nistTechniquesRaytracingExtension;
    j["extensions"] = e;
  }
}

void from_json(json const& j, GLTF& g) {
  g.asset = j.at("asset");
  if (j.find("accessors") != j.end()) {
    g.accessors = j["accessors"].get<decltype(GLTF::accessors)::value_type>();
  }
  if (j.find("buffers") != j.end()) {
    g.buffers = j["buffers"].get<decltype(GLTF::buffers)::value_type>();
  }
  if (j.find("bufferViews") != j.end()) {
    g.bufferViews =
      j["bufferViews"].get<decltype(GLTF::bufferViews)::value_type>();
  }
  if (j.find("images") != j.end()) {
    g.images = j["images"].get<decltype(GLTF::images)::value_type>();
  }
  if (j.find("materials") != j.end()) {
    g.materials = j["materials"].get<decltype(GLTF::materials)::value_type>();
  }
  if (j.find("meshes") != j.end()) {
    g.meshes = j["meshes"].get<decltype(GLTF::meshes)::value_type>();
  }
  if (j.find("nodes") != j.end()) {
    g.nodes = j["nodes"].get<decltype(GLTF::nodes)::value_type>();
  }
  if (j.find("samplers") != j.end()) {
    g.samplers = j["samplers"].get<decltype(GLTF::samplers)::value_type>();
  }
  if (j.find("scene") != j.end()) g.scene = j["scene"];
  if (j.find("scenes") != j.end()) {
    g.scenes = j["scenes"].get<decltype(GLTF::scenes)::value_type>();
  }
  if (j.find("textures") != j.end()) {
    g.textures = j["textures"].get<decltype(GLTF::textures)::value_type>();
  }
  if (j.find("extensionsUsed") != j.end()) {
    g.extensionsUsed =
      j["extensionsUsed"].get<decltype(GLTF::extensionsUsed)::value_type>();
  }
  if (j.find("extensionsRequired") != j.end()) {
    g.extensionsRequired =
      j["extensionsRequired"]
        .get<decltype(GLTF::extensionsRequired)::value_type>();
  }
  if (j.find("extensions") != j.end()) {
    auto&& e = j["extensions"];
    if (e.find("NIST_techniques_raytracing") != e.end()) {
      g.nistTechniquesRaytracingExtension = e["NIST_techniques_raytracing"];
    }
  }
}

inline int AccessorTypeByteSize(std::string const& type) {
  if (type == "SCALAR") return 4;
  if (type == "VEC2") return 8;
  if (type == "VEC3") return 12;
  if (type == "VEC4") return 16;
  if (type == "MAT2") return 16;
  if (type == "MAT3") return 36;
  if (type == "MAT4") return 64;
  return 0;
}

inline std::size_t AccessorComponentTypeSize(int type) {
  switch (type) {
  case 5120: return sizeof(unsigned char);
  case 5121: return sizeof(char);
  case 5122: return sizeof(unsigned short);
  case 5123: return sizeof(short);
  case 5125: return sizeof(unsigned int);
  case 5126: return sizeof(float);
  default: return 0;
  }
}

template <class T, class U>
T GetAccessorDataComponent(gsl::not_null<U*>) {
  IRIS_LOG_CRITICAL("Not implemented");
  std::terminate();
}

template <>
inline unsigned char
GetAccessorDataComponent(gsl::not_null<unsigned char*> bytes) {
  return *bytes;
}

template <>
inline unsigned short
GetAccessorDataComponent(gsl::not_null<unsigned char*> bytes) {
  return static_cast<unsigned short>(*bytes);
}

template <>
inline unsigned int
GetAccessorDataComponent(gsl::not_null<unsigned char*> bytes) {
  return static_cast<unsigned int>(*bytes);
}

template <>
inline unsigned short
GetAccessorDataComponent(gsl::not_null<unsigned short*> bytes) {
  return *bytes;
}

template <>
inline unsigned int
GetAccessorDataComponent(gsl::not_null<unsigned short*> bytes) {
  return static_cast<unsigned int>(*bytes);
}

template <>
inline unsigned int
GetAccessorDataComponent(gsl::not_null<unsigned int*> bytes) {
  return *bytes;
}

template <>
inline glm::vec2 GetAccessorDataComponent(gsl::not_null<float*> bytes) {
  return {*bytes, *(bytes.get() + 1)};
}

template <>
inline glm::vec3 GetAccessorDataComponent(gsl::not_null<float*> bytes) {
  return {*bytes, *(bytes.get() + 1), *(bytes.get() + 2)};
}

template <>
inline glm::vec4 GetAccessorDataComponent(gsl::not_null<float*> bytes) {
  return {*bytes, *(bytes.get() + 1), *(bytes.get() + 2), *(bytes.get() + 3)};
}

template <class T>
expected<std::vector<T>, std::system_error>
GetAccessorData(int index, std::string const& accessorType,
                gsl::span<int> requiredComponentTypes, bool canBeZero,
                std::optional<std::vector<Accessor>> accessors,
                std::optional<std::vector<BufferView>> bufferViews,
                std::vector<std::vector<std::byte>> buffersBytes) {
  std::vector<T> data;

  if (!accessors || accessors->empty()) {
    return unexpected(
      std::system_error(Error::kFileParseFailed, "no accessors"));
  } else if (accessors->size() < static_cast<std::size_t>(index)) {
    return unexpected(
      std::system_error(Error::kFileParseFailed, "too few accessors"));
  }

  auto&& accessor = (*accessors)[index];
  IRIS_LOG_TRACE("accessor: {}", json(accessor).dump());

  if (accessor.type != accessorType) {
    return unexpected(std::system_error(
      Error::kFileParseFailed, "accessor has wrong type '" + accessor.type +
                                 "'; expecting '" + accessorType + "'"));
  }

  if (!requiredComponentTypes.empty()) {
    bool goodComponentType = false;
    for (auto&& componentType : requiredComponentTypes) {
      if (accessor.componentType == componentType) goodComponentType = true;
    }
    if (!goodComponentType) {
      return unexpected(std::system_error(Error::kFileParseFailed,
                                          "accessor has wrong componentType"));
    }
  }

  // The index of the bufferView. When not defined, accessor must be
  // initialized with zeros; sparse property or extensions could override
  // zeros with actual values.
  if (!accessor.bufferView) {
    if (!canBeZero) {
      return unexpected(std::system_error(Error::kFileParseFailed,
                                          "accessor has no bufferView"));
    }

    data.resize(accessor.count);
    std::fill_n(std::begin(data), accessor.count, T{});
    return data;
  }

  if (!bufferViews || bufferViews->empty()) {
    return unexpected(
      std::system_error(Error::kFileParseFailed, "no bufferViews"));
  } else if (bufferViews->size() <
             static_cast<std::size_t>(*accessor.bufferView)) {
    return unexpected(
      std::system_error(Error::kFileParseFailed, "too few bufferViews"));
  }

  auto&& bufferView = (*bufferViews)[*accessor.bufferView];
  IRIS_LOG_TRACE("bufferView: {}", json(bufferView).dump());

  if (buffersBytes.size() < static_cast<std::size_t>(bufferView.buffer)) {
    return unexpected(
      std::system_error(Error::kFileParseFailed, "too few buffers"));
  }

  int const bufferByteStride =
    bufferView.byteStride.value_or(AccessorTypeByteSize(accessorType)) / 4;
  std::size_t const componentTypeSize =
    AccessorComponentTypeSize(accessor.componentType);
  int const byteOffset =
    bufferView.byteOffset.value_or(0) + accessor.byteOffset.value_or(0);

  auto&& bufferBytes = buffersBytes[bufferView.buffer];
  if (bufferBytes.size() < byteOffset + componentTypeSize * accessor.count) {
    return unexpected(
      std::system_error(Error::kFileParseFailed, "buffer too small"));
  }

  std::byte* bytes = bufferBytes.data() + byteOffset;
  data.resize(accessor.count);

  switch (accessor.componentType) {
  case 5120: {
    auto p = reinterpret_cast<char*>(bytes);
    for (int i = 0; i < accessor.count; ++i, p += bufferByteStride) {
      data[i] = GetAccessorDataComponent<T>(gsl::not_null(p));
    }
  } break;

  case 5121: {
    auto p = reinterpret_cast<unsigned char*>(bytes);
    for (int i = 0; i < accessor.count; ++i, p += bufferByteStride) {
      data[i] = GetAccessorDataComponent<T>(gsl::not_null(p));
    }
  } break;

  case 5122: {
    auto p = reinterpret_cast<short*>(bytes);
    for (int i = 0; i < accessor.count; ++i, p += bufferByteStride) {
      data[i] = GetAccessorDataComponent<T>(gsl::not_null(p));
    }
  } break;

  case 5123: {
    auto p = reinterpret_cast<unsigned short*>(bytes);
    for (int i = 0; i < accessor.count; ++i, p += bufferByteStride) {
      data[i] = GetAccessorDataComponent<T>(gsl::not_null(p));
    }
  } break;

  case 5125: {
    auto p = reinterpret_cast<unsigned int*>(bytes);
    for (int i = 0; i < accessor.count; ++i, p += bufferByteStride) {
      data[i] = GetAccessorDataComponent<T>(gsl::not_null(p));
    }
  } break;

  case 5126: {
    auto p = reinterpret_cast<float*>(bytes);
    for (int i = 0; i < accessor.count; ++i, p += bufferByteStride) {
      data[i] = GetAccessorDataComponent<T>(gsl::not_null(p));
    }
  } break;

  default:
    return unexpected(
      std::system_error(Error::kFileParseFailed,
                        "Invalid combination of type and componentType"));
  }

  return data;
} // GetAccessorData

template <class T>
expected<std::vector<T>, std::system_error>
GetAccessorData(int index, std::string const& accessorType,
                int requiredComponentTypes, bool canBeZero,
                std::optional<std::vector<Accessor>> accessors,
                std::optional<std::vector<BufferView>> bufferViews,
                std::vector<std::vector<std::byte>> buffersBytes) {
  return GetAccessorData<T>(index, accessorType, {&requiredComponentTypes, 1},
                            canBeZero, accessors, bufferViews, buffersBytes);
} // GetAccessorData

inline expected<VkPrimitiveTopology, std::system_error>
ModeToVkPrimitiveTopology(std::optional<int> mode) {
  if (!mode) return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  switch (*mode) {
  case 0: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
  case 1: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
  case 3: return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
  case 4: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  case 5: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
  case 6: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
  }

  return unexpected(
    std::system_error(Error::kFileParseFailed, "unknown primitive mode"));
} // glTFModeToVkPrimitiveTopology

static std::vector<glm::vec3>
GenerateNormals(std::vector<glm::vec3> const& positions,
                std::vector<unsigned int> const& indices) noexcept {
  IRIS_LOG_ENTER();
  std::vector<glm::vec3> normals(positions.size());

  if (indices.empty()) {
    IRIS_LOG_DEBUG("Generating normals without indices");
    std::size_t const num = positions.size();
    for (std::size_t i = 0; i < num; i += 3) {
      auto&& a = positions[i];
      auto&& b = positions[i + 1];
      auto&& c = positions[i + 2];
      auto const n = glm::normalize(glm::cross(b - a, c - a));
      normals[i] = n;
      normals[i + 1] = n;
      normals[i + 2] = n;
    }
  } else {
    std::size_t const num = indices.size();
    for (std::size_t i = 0; i < num; i += 3) {
      auto&& a = positions[indices[i]];
      auto&& b = positions[indices[i + 1]];
      auto&& c = positions[indices[i + 2]];
      auto const n = glm::normalize(glm::cross(b - a, c - a));
      normals[indices[i]] = n;
      normals[indices[i + 1]] = n;
      normals[indices[i + 2]] = n;
    }
  }

  IRIS_LOG_LEAVE();
  return normals;
} // GenerateNormals

struct TangentGenerator {
  glm::vec3 const* positions{nullptr};
  glm::vec3 const* normals{nullptr};
  glm::vec2 const* texcoords{nullptr};
  unsigned int const* indices{nullptr};
  std::size_t count{0};
  std::vector<glm::vec4> tangents;

  TangentGenerator(glm::vec3 const* p, glm::vec3 const* n, glm::vec2 const* t,
                   std::size_t c, unsigned int const* i = nullptr)
    : positions(p)
    , normals(n)
    , texcoords(t)
    , indices(i)
    , count(c) {}

  static int GetNumFaces(SMikkTSpaceContext const* pContext) {
    auto pData = reinterpret_cast<TangentGenerator*>(pContext->m_pUserData);
    return gsl::narrow_cast<int>(pData->count / 3);
  }

  static int GetNumVerticesOfFace(SMikkTSpaceContext const*, int const) {
    return 3;
  }

  static void GetPosition(SMikkTSpaceContext const* pContext, float fvPosOut[],
                          int const iFace, int const iVert) {
    auto pData = reinterpret_cast<TangentGenerator*>(pContext->m_pUserData);

    glm::vec3 pos;
    if (pData->indices) {
      pos = pData->positions[pData->indices[iFace * 3 + iVert]];
    } else {
      pos = pData->positions[iFace * 3 + iVert];
    }

    fvPosOut[0] = pos.x;
    fvPosOut[1] = pos.y;
    fvPosOut[2] = pos.z;
  }

  static void GetNormal(SMikkTSpaceContext const* pContext, float fvNormOut[],
                        int const iFace, int const iVert) {
    auto pData = reinterpret_cast<TangentGenerator*>(pContext->m_pUserData);

    glm::vec3 norm;
    if (pData->indices) {
      norm = pData->normals[pData->indices[iFace * 3 + iVert]];
    } else {
      norm = pData->normals[iFace * 3 + iVert];
    }

    fvNormOut[0] = norm.x;
    fvNormOut[1] = norm.y;
    fvNormOut[2] = norm.z;
  }

  static void GetTexCoord(SMikkTSpaceContext const* pContext, float fvTexcOut[],
                          int const iFace, int const iVert) {
    auto pData = reinterpret_cast<TangentGenerator*>(pContext->m_pUserData);

    glm::vec2 texc;
    if (pData->indices) {
      texc = pData->texcoords[pData->indices[iFace * 3 + iVert]];
    } else {
      texc = pData->texcoords[iFace * 3 + iVert];
    }

    fvTexcOut[0] = texc.x;
    fvTexcOut[1] = texc.y;
  }

  static void SetTSpaceBasic(SMikkTSpaceContext const* pContext,
                             float const fvTangent[], float const fSign,
                             int const iFace, int const iVert) {
    auto pData = reinterpret_cast<TangentGenerator*>(pContext->m_pUserData);

    glm::vec4 tangent{fvTangent[0], fvTangent[1], fvTangent[2], fSign};

    if (pData->indices) {
      pData->tangents[pData->indices[iFace * 3 + iVert]] = tangent;
    } else {
      pData->tangents[iFace * 3 + iVert] = tangent;
    }
  }

  bool operator()() noexcept {
    IRIS_LOG_ENTER();
    tangents.resize(count);

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

    bool ret = genTangSpaceDefault(ctx.get());
    IRIS_LOG_LEAVE();
    return ret;
  }
}; // struct TangentGenerator

template <typename T>
expected<std::vector<T>, std::system_error>
GLTF::ParseNode(Renderer::CommandQueue commandQueue, int nodeIdx,
                glm::mat4x4 parentMat, std::filesystem::path const& path,
                std::vector<std::vector<std::byte>> const& buffersBytes,
                std::vector<VkExtent2D> const& imagesExtents,
                std::vector<std::vector<std::byte>> imagesBytes) {
  IRIS_LOG_ENTER();
  std::vector<T> components;

  if (!nodes || nodes->size() < static_cast<std::size_t>(nodeIdx)) {
    return unexpected(
      std::system_error(Error::kFileParseFailed, "not enough nodes"));
  }

  auto&& node = (*nodes)[nodeIdx];
  // Node:
  // std::optional<std::vector<int>> children; // indices into gltf.nodes
  // std::optional<glm::mat4x4> matrix;
  // std::optional<int> mesh; // index into gltf.meshes
  // std::optional<glm::quat> rotation;
  // std::optional<glm::vec3> scale;
  // std::optional<glm::vec3> translation;
  // std::optional<std::string> name;

  IRIS_LOG_TRACE("nodeIdx: {} node: {}", nodeIdx, json(node).dump());
  std::string const nodeName =
    path.string() + ":" + (node.name ? *node.name : fmt::format("{}", nodeIdx));

  // TODO: Translate the GLTF camera matrix space into Nav matrix space
  // if (node.name && *node.name == "Camera") {
  // if (node.rotation) Renderer::Nav::Reorient(*node.rotation);
  // if (node.translation) Renderer::Nav::Reposition(*node.translation);
  // if (node.scale) Renderer::Nav::Rescale((*node.scale)[0]);
  //}

  glm::mat4x4 nodeMat = parentMat;

  if (node.matrix) {
    nodeMat *= *node.matrix;
    if (node.translation || node.rotation || node.scale) {
      IRIS_LOG_WARN("node has both matrix and TRS; using matrix");
    }
  } else {
    if (node.translation) nodeMat *= glm::translate({}, *node.translation);
    if (node.rotation) nodeMat *= glm::mat4_cast(*node.rotation);
    if (node.scale) nodeMat *= glm::scale({}, *node.scale);
  }

  auto&& children =
    node.children.value_or(decltype(gltf::Node::children)::value_type({}));

  for (auto&& child : children) {
    if (auto c = ParseNode<T>(commandQueue, child, nodeMat, path, buffersBytes,
                              imagesExtents, imagesBytes)) {
      components.insert(components.end(), c->begin(), c->end());
    } else {
      IRIS_LOG_LEAVE();
      return unexpected(c.error());
    }
  }

  if (node.shaderToy) {
    if (auto c = ParsePrimitive<T>(commandQueue, "", nodeMat, buffersBytes,
                                   imagesExtents, imagesBytes, node, {})) {
      components.push_back(std::move(*c));
    } else {
      return unexpected(c.error());
    }
  }

  if (!node.mesh) {
    IRIS_LOG_ENTER();
    return components;
  }

  if (!meshes || meshes->empty()) {
    IRIS_LOG_LEAVE();
    return unexpected(std::system_error(Error::kFileParseFailed,
                                        "node defines mesh, but no meshes"));
  }

  if (meshes->size() < static_cast<std::size_t>(*node.mesh)) {
    IRIS_LOG_LEAVE();
    return unexpected(std::system_error(
      Error::kFileParseFailed, "node defines mesh, but not enough meshes"));
  }

  auto&& mesh = (*meshes)[*node.mesh];
  // Mesh:
  // std::vector<Primitive> primitives;

  IRIS_LOG_TRACE("mesh: {}", json(mesh).dump());

  for (std::size_t primIdx = 0; primIdx < mesh.primitives.size(); ++primIdx) {
    auto&& primitive = mesh.primitives[primIdx];
    // Primitive:
    // std::map<std::string, int> attributes; // index into gltf.accessors
    // std::optional<int> indices;            // index into gltf.accessors
    // std::optional<int> material;           // index into gltf.materials
    // std::optional<int> mode;
    // std::optional<std::vector<int>> targets;
    //
    // From the glTF 2.0 spec:
    //
    // Implementation note: Each primitive corresponds to one WebGL draw
    // call (engines are, of course, free to batch draw calls). When a
    // primitive's indices property is defined, it references the accessor
    // to use for index data, and GL's drawElements function should be used.
    // When the indices property is not defined, GL's drawArrays function
    // should be used with a count equal to the count property of any of the
    // accessors referenced by the attributes property (they are all equal
    // for a given primitive).
    //
    // Implementation note: When positions are not specified, client
    // implementations should skip primitive's rendering unless its
    // positions are provided by other means (e.g., by extension). This
    // applies to both indexed and non-indexed geometry.
    //
    // Implementation note: When normals are not specified, client
    // implementations should calculate flat normals.
    //
    // Implementation note: When tangents are not specified, client
    // implementations should calculate tangents using default MikkTSpace
    // algorithms. For best results, the mesh triangles should also be
    // processed using default MikkTSpace algorithms.
    //
    // Implementation note: Vertices of the same triangle should have the
    // same tangent.w value. When vertices of the same triangle have
    // different tangent.w values, tangent space is considered undefined.
    //
    // Implementation note: When normals and tangents are specified, client
    // implementations should compute the bitangent by taking the cross
    // product of the normal and tangent xyz vectors and multiplying against
    // the w component of the tangent: bitangent = cross(normal,
    // tangent.xyz) * tangent.w

    std::string const meshName =
      nodeName + ":" + (mesh.name ? *mesh.name : fmt::format("{}", primIdx));

    if (auto c =
          ParsePrimitive<T>(commandQueue, meshName, nodeMat, buffersBytes,
                            imagesExtents, imagesBytes, node, primitive)) {
      components.push_back(std::move(*c));
    } else {
      return unexpected(c.error());
    }
  }

  IRIS_LOG_LEAVE();
  return components;
} // GLTF::ParseNode

template <>
iris::expected<iris::Renderer::Component::Renderable, std::system_error>
GLTF::ParsePrimitive(Renderer::CommandQueue commandQueue,
                     std::string const& meshName, glm::mat4x4 const& nodeMat,
                     std::vector<std::vector<std::byte>> const& buffersBytes,
                     std::vector<VkExtent2D> const& imagesExtents,
                     std::vector<std::vector<std::byte>> const& imagesBytes,
                     Node const& node, Primitive const& primitive) {
  IRIS_LOG_ENTER();

  if (node.shaderToy) {
    if (node.shaderToy->url) {
      if (auto c = io::LoadShaderToy(*node.shaderToy->url)) {
        IRIS_LOG_LEAVE();
        return std::move(*c);
      } else {
        IRIS_LOG_LEAVE();
        return unexpected(c.error());
      }
    } else {
      IRIS_LOG_LEAVE();
      return unexpected(
        std::system_error(Error::kFileParseFailed,
                          "node has unsupported shaderToy type (not url)"));
    }
  }

  //
  // First, get the positions
  //
  std::vector<glm::vec3> positions;
  for (auto&& [semantic, index] : primitive.attributes) {
    if (semantic == "POSITION") {
      IRIS_LOG_TRACE("reading POSITION");
      if (auto p = gltf::GetAccessorData<glm::vec3>(
            index, "VEC3", 5126, true, accessors, bufferViews, buffersBytes)) {
        positions = std::move(*p);
      } else {
        IRIS_LOG_LEAVE();
        return unexpected(p.error());
      }
    }
  }

  // primitives with no positions are "ignored"
  if (positions.empty()) return {};

  // Next, get the indices if present. We're only getting the indices here
  // to use them for possible normal/tangent generation. We will use the
  // original format of the indices in the draw call.
  std::vector<unsigned int> indices;
  if (primitive.indices) {
    IRIS_LOG_TRACE("reading indices");
    std::array<int, 2> componentTypes{5123, 5125};
    if (auto i = gltf::GetAccessorData<unsigned int>(
          *primitive.indices, "SCALAR", componentTypes, false, accessors,
          bufferViews, buffersBytes)) {
      indices = std::move(*i);
    } else {
      IRIS_LOG_LEAVE();
      return unexpected(i.error());
    }
  }

  //
  // Now get texcoords, normals, and tangents
  //
  std::vector<glm::vec2> texcoords;
  std::vector<glm::vec3> normals;
  std::vector<glm::vec4> tangents;

  for (auto&& [semantic, index] : primitive.attributes) {
    if (semantic == "TEXCOORD_0") {
      IRIS_LOG_TRACE("reading TEXCOORD_0");
      if (auto t = gltf::GetAccessorData<glm::vec2>(
            index, "VEC2", 5126, true, accessors, bufferViews, buffersBytes)) {
        texcoords = std::move(*t);
      } else {
        IRIS_LOG_LEAVE();
        return unexpected(t.error());
      }
    } else if (semantic == "NORMAL") {
      IRIS_LOG_TRACE("reading NORMAL");
      if (auto n = gltf::GetAccessorData<glm::vec3>(
            index, "VEC3", 5126, true, accessors, bufferViews, buffersBytes)) {
        normals = std::move(*n);
      } else {
        IRIS_LOG_LEAVE();
        return unexpected(n.error());
      }
    } else if (semantic == "TANGENT") {
      IRIS_LOG_TRACE("reading TANGENT");
      if (auto t = gltf::GetAccessorData<glm::vec4>(
            index, "VEC4", 5126, true, accessors, bufferViews, buffersBytes)) {
        tangents = std::move(*t);
      } else {
        IRIS_LOG_LEAVE();
        return unexpected(t.error());
      }
    }
  }

  decltype(Renderer::Component::Material::vertexInputAttributeDescriptions)
    vertexInputAttributeDescriptions;
  std::uint32_t vertexStride = 0;

  vertexInputAttributeDescriptions.push_back({
    0,                          // location
    0,                          // binding
    VK_FORMAT_R32G32B32_SFLOAT, // format
    vertexStride                // offset
  });
  vertexStride += sizeof(glm::vec3);

  if (normals.empty()) {
    IRIS_LOG_WARN("GLTF model with no normals: generating");
    normals = GenerateNormals(positions, indices);
  }

  vertexInputAttributeDescriptions.push_back({
    1,                          // location
    0,                          // binding
    VK_FORMAT_R32G32B32_SFLOAT, // format
    vertexStride                // offset
  });
  vertexStride += sizeof(glm::vec3);

  if (tangents.empty() && !texcoords.empty()) {
    IRIS_LOG_WARN("GLTF model with texcoords but no tangents: generating");
    TangentGenerator tg(positions.data(), normals.data(), texcoords.data(),
                        indices.empty() ? positions.size() : indices.size(),
                        indices.empty() ? nullptr : indices.data());

    if (tg()) {
      tangents = std::move(tg.tangents);
    } else {
      IRIS_LOG_LEAVE();
      return unexpected(std::system_error(Error::kFileLoadFailed,
                                          "unable to generate tangent space"));
    }
  }

  if (!tangents.empty()) {
    vertexInputAttributeDescriptions.push_back({
      2,                             // location
      0,                             // binding
      VK_FORMAT_R32G32B32A32_SFLOAT, // format
      vertexStride                   // offset
    });
    vertexStride += sizeof(glm::vec4);
  }

  if (!texcoords.empty()) {
    vertexInputAttributeDescriptions.push_back({
      3,                       // location
      0,                       // binding
      VK_FORMAT_R32G32_SFLOAT, // format
      vertexStride             // offset
    });
    vertexStride += sizeof(glm::vec2);
  }

  decltype(Renderer::Component::Material::vertexInputBindingDescriptions)
    vertexInputBindingDescriptions{{
      0,                          // binding
      vertexStride,               // stride
      VK_VERTEX_INPUT_RATE_VERTEX // inputRate
    }};

  Renderer::Component::Renderable component;

  if (auto t = gltf::ModeToVkPrimitiveTopology(primitive.mode)) {
    component.topology = *t;
  } else {
    IRIS_LOG_LEAVE();
    return unexpected(t.error());
  }

  VkFrontFace const frontFace = (glm::determinant(nodeMat) < 0.f)
                                  ? VK_FRONT_FACE_CLOCKWISE
                                  : VK_FRONT_FACE_COUNTER_CLOCKWISE;

  if (primitive.material) {
    if (auto it = materialsMap.find(*primitive.material);
        it != materialsMap.end()) {
      component.material = it->second;
    } else {
      if (auto m = CreateMaterial(
            commandQueue, meshName, component.topology, !texcoords.empty(),
            vertexInputBindingDescriptions, vertexInputAttributeDescriptions,
            frontFace, *primitive.material, imagesExtents, imagesBytes)) {
        auto matID = Renderer::AddMaterial(std::move(*m));
        materialsMap.insert(std::make_pair(*primitive.material, matID));
        component.material = matID;
      } else {
        IRIS_LOG_LEAVE();
        return unexpected(m.error());
      }
    }
  }

  VkDeviceSize const vertexBufferSize = vertexStride * positions.size();

  auto staging =
    AllocateBuffer(vertexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                   VMA_MEMORY_USAGE_CPU_TO_GPU);
  if (!staging) {
    IRIS_LOG_LEAVE();
    return unexpected(staging.error());
  }

  float* pVertexBuffer;
  if (auto ptr = staging->Map<float*>()) {
    pVertexBuffer = *ptr;
  } else {
    DestroyBuffer(*staging);
    IRIS_LOG_LEAVE();
    return unexpected(ptr.error());
  }

  for (std::size_t i = 0; i < positions.size(); i++) {
    *pVertexBuffer++ = positions[i].x;
    *pVertexBuffer++ = positions[i].y;
    *pVertexBuffer++ = positions[i].z;
    *pVertexBuffer++ = normals[i].x;
    *pVertexBuffer++ = normals[i].y;
    *pVertexBuffer++ = normals[i].z;

    if (!tangents.empty()) {
      *pVertexBuffer++ = tangents[i].x;
      *pVertexBuffer++ = tangents[i].y;
      *pVertexBuffer++ = tangents[i].z;
      *pVertexBuffer++ = tangents[i].w;
    }

    if (!texcoords.empty()) {
      *pVertexBuffer++ = texcoords[i].x;
      *pVertexBuffer++ = texcoords[i].y;
    }
  }

  staging->Unmap();

  if (auto buf = AllocateBuffer(vertexBufferSize,
                                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                                  VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                VMA_MEMORY_USAGE_GPU_ONLY)) {
    component.vertexBuffer = *buf;
  } else {
    DestroyBuffer(*staging);
    IRIS_LOG_LEAVE();
    return unexpected(buf.error());
  }

  VkCommandBuffer commandBuffer;
  if (auto cb = Renderer::BeginOneTimeSubmit(commandQueue.commandPool)) {
    commandBuffer = *cb;
  } else {
    DestroyBuffer(component.vertexBuffer);
    DestroyBuffer(*staging);
    IRIS_LOG_LEAVE();
    return unexpected(cb.error());
  }

  VkBufferCopy region = {};
  region.srcOffset = 0;
  region.dstOffset = 0;
  region.size = vertexBufferSize;

  vkCmdCopyBuffer(commandBuffer, staging->buffer, component.vertexBuffer.buffer,
                  1, &region);

  if (auto result = Renderer::EndOneTimeSubmit(
        commandBuffer, commandQueue.commandPool, commandQueue.queue,
        commandQueue.submitFence);
      !result) {
    DestroyBuffer(component.vertexBuffer);
    DestroyBuffer(*staging);
    IRIS_LOG_LEAVE();
    return unexpected(result.error());
  }

  if (!primitive.indices) {
    component.numVertices = gsl::narrow_cast<std::uint32_t>(positions.size());
  } else {
    auto indexAccessor = (*accessors)[*primitive.indices];

    VkDeviceSize const indexBufferSize =
      indexAccessor.count * (indexAccessor.componentType == 5123
                               ? sizeof(std::uint16_t)
                               : sizeof(std::uint32_t));

    staging = ReallocateBuffer(*staging, indexBufferSize,
                               VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                               VMA_MEMORY_USAGE_CPU_TO_GPU);
    if (!staging) {
      DestroyBuffer(component.vertexBuffer);
      IRIS_LOG_LEAVE();
      return unexpected(staging.error());
    }

    std::byte* pIndexBuffer;
    if (auto ptr = staging->Map<std::byte*>()) {
      pIndexBuffer = *ptr;
    } else {
      IRIS_LOG_LEAVE();
      return unexpected(ptr.error());
    }

    auto indexBufferView = (*bufferViews)[*indexAccessor.bufferView];
    auto indexBufferSrc = (*buffers)[indexBufferView.buffer];

    std::memcpy(pIndexBuffer,
                buffersBytes[indexBufferView.buffer].data() +
                  indexBufferView.byteOffset.value_or(0),
                indexBufferSize);

    staging->Unmap();

    if (auto buf = AllocateBuffer(indexBufferSize,
                                  VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                                    VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                  VMA_MEMORY_USAGE_GPU_ONLY)) {
      component.indexBuffer = *buf;
    } else {
      DestroyBuffer(component.vertexBuffer);
      DestroyBuffer(*staging);
      IRIS_LOG_LEAVE();
      return unexpected(buf.error());
    }

    if (auto cb = Renderer::BeginOneTimeSubmit(commandQueue.commandPool)) {
      commandBuffer = *cb;
    } else {
      DestroyBuffer(component.indexBuffer);
      DestroyBuffer(component.vertexBuffer);
      DestroyBuffer(*staging);
      IRIS_LOG_LEAVE();
      return unexpected(cb.error());
    }

    region = {};
    region.srcOffset = 0;
    region.dstOffset = 0;
    region.size = indexBufferSize;

    vkCmdCopyBuffer(commandBuffer, staging->buffer,
                    component.indexBuffer.buffer, 1, &region);

    if (auto result = Renderer::EndOneTimeSubmit(
          commandBuffer, commandQueue.commandPool, commandQueue.queue,
          commandQueue.submitFence);
        !result) {
      DestroyBuffer(component.indexBuffer);
      DestroyBuffer(component.vertexBuffer);
      DestroyBuffer(*staging);
      IRIS_LOG_LEAVE();
      return unexpected(result.error());
    }

    component.indexType =
      (indexAccessor.componentType == 5123 ? VK_INDEX_TYPE_UINT16
                                           : VK_INDEX_TYPE_UINT32);
    component.numIndices = indexAccessor.count;
  }

  component.modelMatrix = nodeMat;

  // Compute the bounding sphere
  struct CoordAccessor {
    using Pit = decltype(positions)::const_iterator;
    using Cit = float const*;
    inline Cit operator()(Pit it) const { return glm::value_ptr(*it); }
  };

#if PLATFORM_COMPILER_MSVC
#pragma warning(push)
#pragma warning(disable : 4458)
#elif PLATFORM_COMPILER_GCC
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#endif

  Miniball::Miniball<CoordAccessor> mb(3, positions.begin(), positions.end());

  component.boundingSphere =
    glm::vec4(mb.center()[0], mb.center()[1], mb.center()[2],
              std::sqrt(mb.squared_radius()));
  IRIS_LOG_DEBUG("boundingSphere: ({} {} {}), {}", component.boundingSphere.x,
                 component.boundingSphere.y, component.boundingSphere.z,
                 component.boundingSphere.w);

#if PLATFORM_COMPILER_MSVC
#pragma warning(pop)
#elif PLATFORM_COMPILER_GCC
#pragma GCC diagnostic pop
#endif

  IRIS_LOG_LEAVE();
  return component;
} // GLTF::ParsePrimitive

template <>
iris::expected<iris::Renderer::Component::Traceable::Geometry,
               std::system_error>
GLTF::ParsePrimitive(Renderer::CommandQueue commandQueue, std::string const&,
                     glm::mat4x4 const&,
                     std::vector<std::vector<std::byte>> const& buffersBytes,
                     std::vector<VkExtent2D> const&,
                     std::vector<std::vector<std::byte>> const&, Node const&,
                     Primitive const& primitive) {
  IRIS_LOG_ENTER();

  Renderer::Component::Traceable::Geometry geom;

  //
  // TODO: CreateSpheres
  //

  std::vector<glm::vec3> aabbs;

  for (auto&& [semantic, index] : primitive.attributes) {
    if (semantic == "_AABB") {
      IRIS_LOG_TRACE("reading _AABB");
      if (auto a = gltf::GetAccessorData<glm::vec3>(
            index, "VEC3", 5126, false, accessors, bufferViews, buffersBytes)) {
        aabbs = std::move(*a);
      } else {
        IRIS_LOG_LEAVE();
        return unexpected(a.error());
      }
    }
  }

  IRIS_LOG_DEBUG("aabbs.size(): {}", aabbs.size());

  if (auto buf =
        iris::CreateBuffer(commandQueue.commandPool,           // commandPool
                           commandQueue.queue,                 // queue
                           commandQueue.submitFence,           // fence
                           VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, // bufferUsage
                           VMA_MEMORY_USAGE_GPU_ONLY,          // memoryUsage
                           aabbs.size() * sizeof(glm::vec3),   // size
                           reinterpret_cast<std::byte*>(aabbs.data()) // data
                           )) {
    geom.buffer = *buf;
  } else {
    IRIS_LOG_LEAVE();
    return unexpected(buf.error());
  }

  //
  // TODO: CreateGeometry
  //

  VkGeometryTrianglesNV triangles = {};
  triangles.sType = VK_STRUCTURE_TYPE_GEOMETRY_TRIANGLES_NV;

  VkGeometryAABBNV spheres = {};
  spheres.sType = VK_STRUCTURE_TYPE_GEOMETRY_AABB_NV;
  spheres.pNext = nullptr;
  spheres.aabbData = geom.buffer.buffer;
  spheres.numAABBs = gsl::narrow_cast<std::uint32_t>(aabbs.size() / 2);
  spheres.stride = sizeof(glm::vec3) * 2;
  spheres.offset = 0;

  geom.geometry.sType = VK_STRUCTURE_TYPE_GEOMETRY_NV;
  geom.geometry.geometryType = VK_GEOMETRY_TYPE_AABBS_NV;
  geom.geometry.geometry.triangles = triangles;
  geom.geometry.geometry.aabbs = spheres;
  geom.geometry.flags = VK_GEOMETRY_OPAQUE_BIT_NV;

  //
  // TODO: CreateBottomLevelAccelerationStructure
  //

  if (auto structure = CreateBottomLevelAccelerationStructure(
        gsl::make_span(&geom.geometry, 1), 0)) {
    geom.bottomLevelAccelerationStructure = *structure;
  } else {
    IRIS_LOG_LEAVE();
    return unexpected(structure.error());
  }

  IRIS_LOG_LEAVE();
  return geom;
} // GLTF::ParsePrimitive

iris::expected<
  std::tuple<VkDescriptorSetLayout, VkDescriptorSet,
             absl::InlinedVector<iris::ShaderGroup, 8>, iris::Pipeline>,
  std::system_error>
GLTF::ParseRaytracingPipeline(int numGeometries) {
  IRIS_LOG_ENTER();

  absl::InlinedVector<VkDescriptorSetLayoutBinding, 128> bindings{
    {
      0,                                            // binding
      VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV, // descriptorType
      1,                                            // descriptorCount
      VK_SHADER_STAGE_RAYGEN_BIT_NV | VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV |
        VK_SHADER_STAGE_ANY_HIT_BIT_NV |
        VK_SHADER_STAGE_INTERSECTION_BIT_NV, // stageFlags
      nullptr                                // pImmutableSamplers
    },
    {
      1,                                // binding
      VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, // descriptorType
      1,                                // descriptorCount
      VK_SHADER_STAGE_RAYGEN_BIT_NV | VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV |
        VK_SHADER_STAGE_ANY_HIT_BIT_NV |
        VK_SHADER_STAGE_INTERSECTION_BIT_NV, // stageFlags
      nullptr                                // pImmutableSamplers
    },
  };

  for (int i = 0; i < numGeometries; ++i) {
    auto const binding = gsl::narrow_cast<std::uint32_t>(bindings.size());
    bindings.push_back({
      binding,
      VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, // descriptorType
      1,                                 // descriptorCount
      VK_SHADER_STAGE_RAYGEN_BIT_NV | VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV |
        VK_SHADER_STAGE_ANY_HIT_BIT_NV |
        VK_SHADER_STAGE_INTERSECTION_BIT_NV, // stageFlags
      nullptr                                // pImmutableSamplers
    });
  }

  VkDescriptorSetLayoutCreateInfo layoutCI = {};
  layoutCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layoutCI.bindingCount = gsl::narrow_cast<std::uint32_t>(bindings.size());
  layoutCI.pBindings = bindings.data();

  VkDescriptorSetLayout descriptorSetLayout;
  if (auto result = vkCreateDescriptorSetLayout(Renderer::sDevice, &layoutCI,
                                                nullptr, &descriptorSetLayout);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return unexpected(std::system_error(make_error_code(result),
                                        "Cannot create descriptor set layout"));
  }

  VkDescriptorSetAllocateInfo setAI = {};
  setAI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  setAI.descriptorPool = Renderer::sDescriptorPool;
  setAI.descriptorSetCount = 1;
  setAI.pSetLayouts = &descriptorSetLayout;

  VkDescriptorSet descriptorSet;
  if (auto result =
        vkAllocateDescriptorSets(Renderer::sDevice, &setAI, &descriptorSet);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return unexpected(std::system_error(make_error_code(result),
                                        "Cannot allocate descriptor set"));
  }

  if (!nistTechniquesRaytracingExtension) {
    IRIS_LOG_LEAVE();
    return unexpected(
      std::system_error(Error::kFileParseFailed,
                        "File has no NIST_techniques_raytracing extension"));
  }

  auto&& shaders = nistTechniquesRaytracingExtension->shaders;
  auto&& sbt = nistTechniquesRaytracingExtension->shaderBindingTable;

  if (static_cast<std::size_t>(sbt.raygenShader) > shaders.size() ||
      static_cast<std::size_t>(sbt.missShader) > shaders.size()) {
    IRIS_LOG_LEAVE();
    return unexpected(
      std::system_error(Error::kFileParseFailed,
                        "NIST_techniques_raytracing has too few shaders"));
  }

  for (auto&& hitShaders : sbt.hitShaders) {
    for (auto&& shader : hitShaders) {
      if (static_cast<std::size_t>(shader.second) > shaders.size()) {
        IRIS_LOG_LEAVE();
        return unexpected(
          std::system_error(Error::kFileParseFailed,
                            "NIST_techniques_raytracing has too few shaders"));
      }
    }
  }

  absl::InlinedVector<Shader, 16> compiledShaders;
  absl::InlinedVector<ShaderGroup, 8> shaderGroups;

  if (auto s = LoadShaderFromFile(shaders[sbt.raygenShader].uri,
                                  VK_SHADER_STAGE_RAYGEN_BIT_NV)) {
    compiledShaders.push_back(*s);
    shaderGroups.push_back(ShaderGroup::General(
      gsl::narrow_cast<std::uint32_t>(compiledShaders.size() - 1)));
  } else {
    IRIS_LOG_LEAVE();
    return unexpected(s.error());
  }

  if (auto s = LoadShaderFromFile(shaders[sbt.missShader].uri,
                                  VK_SHADER_STAGE_MISS_BIT_NV)) {
    compiledShaders.push_back(*s);
    shaderGroups.push_back(ShaderGroup::General(
      gsl::narrow_cast<std::uint32_t>(compiledShaders.size() - 1)));
  } else {
    IRIS_LOG_LEAVE();
    return unexpected(s.error());
  }

  for (auto&& hitShaders : sbt.hitShaders) {
    if (hitShaders.find("intersectionShader") != hitShaders.end()) {
      if (auto s =
            LoadShaderFromFile(shaders[hitShaders["intersectionShader"]].uri,
                               VK_SHADER_STAGE_INTERSECTION_BIT_NV)) {
        compiledShaders.push_back(*s);
      } else {
        IRIS_LOG_LEAVE();
        return unexpected(s.error());
      }

      auto&& intersectionIndex =
        gsl::narrow_cast<std::uint32_t>(compiledShaders.size() - 1);

      if (auto s =
            LoadShaderFromFile(shaders[hitShaders["closestHitShader"]].uri,
                               VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV)) {
        compiledShaders.push_back(*s);
      } else {
        IRIS_LOG_LEAVE();
        return unexpected(s.error());
      }

      shaderGroups.push_back(ShaderGroup::ProceduralHit(
        intersectionIndex,
        static_cast<std::uint32_t>(compiledShaders.size() - 1)));
    } else {
      IRIS_LOG_WARN("non-intersection hit shader group not implemented.");
    }
  }

  for (std::size_t i = 0; i < shaderGroups.size(); ++i) {
    IRIS_LOG_DEBUG("shaderGroups[{}]: {} {} {}", i,
                   shaderGroups[i].generalShaderIndex,
                   shaderGroups[i].intersectionShaderIndex,
                   shaderGroups[i].closestHitShaderIndex);
  }

  if (auto p =
        CreateRayTracingPipeline(compiledShaders, shaderGroups,
                                 gsl::make_span(&descriptorSetLayout, 1), 1)) {
    return std::make_tuple(descriptorSetLayout, descriptorSet, shaderGroups,
                           *p);
  } else {
    IRIS_LOG_LEAVE();
    return unexpected(p.error());
  }
} // GLTF::ParseRaytracingPipeline

expected<void, std::system_error>
GLTF::ParseRaytracingMaterials(gsl::span<ShaderGroup> shaderGroups) {
  IRIS_LOG_ENTER();

  if (!materials) {
    IRIS_LOG_LEAVE();
    return unexpected(
      std::system_error(Error::kFileParseFailed, "no materials"));
  }

  for (auto&& material : *materials) {
    if (!material.nistTechniquesRaytracingExtension) continue;

    if (material.nistTechniquesRaytracingExtension->hitShaders + 2 >
        shaderGroups.size()) {
      IRIS_LOG_LEAVE();
      return unexpected(std::system_error(
        Error::kFileParseFailed,
        "NIST_techniques_raytracing has too few shaderGroups"));
    }

    //auto&& shaderGroup =
      //shaderGroups[material.nistTechniquesRaytracingExtension->hitShaders +
                   //2];

    //shaderGroup.type;
  }

  IRIS_LOG_LEAVE();
  return {};
} // GLTF::ParseRaytracingMaterials

expected<GLTF::DeviceTexture, std::system_error> GLTF::CreateTexture(
  Renderer::CommandQueue commandQueue, TextureInfo textureInfo,
  std::vector<VkExtent2D> const& imagesExtents,
  std::vector<std::vector<std::byte>> imagesBytes, bool srgb) {
  IRIS_LOG_ENTER();
  DeviceTexture deviceTexture;

  if (!textures ||
      static_cast<std::size_t>(textureInfo.index) >= textures->size()) {
    IRIS_LOG_LEAVE();
    return unexpected(std::system_error(
      Error::kFileParseFailed, "material references non-existent texture"));
  }

  auto&& texture = (*textures)[textureInfo.index];
  auto&& extent = imagesExtents[*texture.source];
  auto&& bytes = imagesBytes[*texture.source];
  std::uint32_t const nLevels =
    1 + gsl::narrow_cast<std::uint32_t>(
          std::floor(std::log2(std::max(extent.width, extent.height))));

  absl::InlinedVector<std::size_t, 16> mipLevelSizes(nLevels);
  mipLevelSizes[0] = extent.width * extent.height * sizeof(std::byte) * 4;

  absl::InlinedVector<VkExtent2D, 16> mipLevelExtents(nLevels);
  mipLevelExtents[0] = extent;

  std::size_t totalBytesSize = mipLevelSizes[0];

  for (uint32_t i = 1; i < nLevels; ++i) {
    mipLevelExtents[i].width = mipLevelExtents[i - 1].width / 2;
    mipLevelExtents[i].height = mipLevelExtents[i - 1].height / 2;
    mipLevelSizes[i] = static_cast<std::size_t>(mipLevelExtents[i].width) *
                       static_cast<std::size_t>(mipLevelExtents[i].height) *
                       sizeof(std::byte) * 4;
    totalBytesSize += mipLevelSizes[i];
  }

  IRIS_LOG_DEBUG("extent: ({}x{}) nLevels: {} totalBytesSize: {}", extent.width,
                 extent.height, nLevels, totalBytesSize);

  std::vector<std::byte> mipLevelBytes(totalBytesSize);
  std::copy_n(bytes.data(), mipLevelSizes[0], mipLevelBytes.data());

  std::size_t offset = mipLevelSizes[0];
  for (uint32_t i = 1; i < nLevels; ++i) {
    IRIS_LOG_DEBUG("creating mip level {} ({}x{}) {} {}", i,
                   mipLevelExtents[i].width, mipLevelExtents[i].height,
                   mipLevelSizes[i], offset);

    auto src = reinterpret_cast<unsigned char*>(mipLevelBytes.data() + offset -
                                                mipLevelSizes[i - 1]);
    auto dst = reinterpret_cast<unsigned char*>(mipLevelBytes.data() + offset);

    if (srgb) {
      stbir_resize_uint8_srgb(
        src, mipLevelExtents[i - 1].width, mipLevelExtents[i - 1].height, 0,
        dst, mipLevelExtents[i].width, mipLevelExtents[i].height, 0, 4, 3, 0);
    } else {
      stbir_resize_uint8(
        src, mipLevelExtents[i - 1].width, mipLevelExtents[i - 1].height, 0,
        dst, mipLevelExtents[i].width, mipLevelExtents[i].height, 0, 4);
    }

    offset += mipLevelSizes[i];
  }

  if (!texture.source ||
      static_cast<std::size_t>(*texture.source) >= imagesBytes.size()) {
    IRIS_LOG_LEAVE();
    return unexpected(std::system_error(
      Error::kFileParseFailed, "texture references non-existent source"));
  }

  if (auto tex = CreateImage(
        commandQueue.commandPool, commandQueue.queue, commandQueue.submitFence,
        VK_FORMAT_R8G8B8A8_UNORM, mipLevelExtents, VK_IMAGE_USAGE_SAMPLED_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY, gsl::not_null(mipLevelBytes.data()), 4)) {
    deviceTexture.texture = *tex;
  } else {
    IRIS_LOG_LEAVE();
    return unexpected(tex.error());
  }

  if (auto view = CreateImageView(
        deviceTexture.texture, VK_IMAGE_VIEW_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM,
        {VK_IMAGE_ASPECT_COLOR_BIT, 0, nLevels, 0, 1})) {
    deviceTexture.view = *view;
  } else {
    IRIS_LOG_LEAVE();
    return unexpected(view.error());
  }

  VkSamplerCreateInfo samplerCI = {};
  samplerCI.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerCI.magFilter = VK_FILTER_LINEAR; // auto ??
  samplerCI.minFilter = VK_FILTER_LINEAR; // auto ??
  samplerCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  samplerCI.mipLodBias = 0.f;
  samplerCI.anisotropyEnable = VK_FALSE;
  samplerCI.maxAnisotropy = 1;
  samplerCI.compareEnable = VK_FALSE;
  samplerCI.compareOp = VK_COMPARE_OP_ALWAYS;
  samplerCI.minLod = -1000.f;
  samplerCI.maxLod = 1000.f;
  samplerCI.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
  samplerCI.unnormalizedCoordinates = VK_FALSE;

  if (texture.sampler) {
    auto&& sampler = (*samplers)[*texture.sampler];

    switch (sampler.magFilter.value_or(9720)) {
    case 9728: samplerCI.magFilter = VK_FILTER_NEAREST; break;
    case 9729: samplerCI.magFilter = VK_FILTER_LINEAR; break;
    }

    switch (sampler.minFilter.value_or(9720)) {
    case 9728:
      samplerCI.minFilter = VK_FILTER_NEAREST;
      samplerCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
      samplerCI.minLod = 0.0;
      samplerCI.maxLod = 0.25;
      break;
    case 9729:
      samplerCI.minFilter = VK_FILTER_LINEAR;
      samplerCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
      samplerCI.minLod = 0.0;
      samplerCI.maxLod = 0.25;
      break;
    case 9984:
      samplerCI.minFilter = VK_FILTER_NEAREST;
      samplerCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
      break;
    case 9985:
      samplerCI.minFilter = VK_FILTER_LINEAR;
      samplerCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
      break;
    case 9986:
      samplerCI.minFilter = VK_FILTER_NEAREST;
      samplerCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
      break;
    case 9987:
      samplerCI.minFilter = VK_FILTER_LINEAR;
      samplerCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
      break;
    }

    switch (sampler.wrapS.value_or(10497)) {
    case 10497: samplerCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT; break;
    case 33071:
      samplerCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
      break;
    case 33648:
      samplerCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
      break;
    }

    switch (sampler.wrapT.value_or(10497)) {
    case 10497: samplerCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT; break;
    case 33071:
      samplerCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
      break;
    case 33648:
      samplerCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
      break;
    }
  }

  VkSampler sampler;
  if (auto result =
        vkCreateSampler(Renderer::sDevice, &samplerCI, nullptr, &sampler);
      result == VK_SUCCESS) {
    deviceTexture.sampler = sampler;
  } else {
    IRIS_LOG_LEAVE();
    return unexpected(std::system_error(make_error_code(result),
                                        "Cannot create texture sampler"));
  }

  IRIS_LOG_LEAVE();
  return deviceTexture;
} // GLTF::CreateTexture

expected<Renderer::Component::Material, std::system_error> GLTF::CreateMaterial(
  Renderer::CommandQueue commandQueue, std::string const& meshName,
  VkPrimitiveTopology topology, bool hasTexCoords,
  decltype(Renderer::Component::Material::vertexInputBindingDescriptions)
    vertexInputBindingDescriptions,
  decltype(Renderer::Component::Material::vertexInputAttributeDescriptions)
    vertexInputAttributeDescriptions,
  VkFrontFace frontFace, int materialIndex,
  std::vector<VkExtent2D> const& imagesExtents,
  std::vector<std::vector<std::byte>> const& imagesBytes) {
  IRIS_LOG_ENTER();

  int baseColorIndex = -1;
  int metallicRoughnessIndex = -1;
  int normalIndex = -1;
  int emissiveIndex = -1;
  int occlusionIndex = -1;

  absl::InlinedVector<std::string, 8> shaderMacros;
  if (hasTexCoords) shaderMacros.push_back("#define HAS_TEXCOORDS");

  absl::InlinedVector<VkDescriptorSetLayoutBinding,
                      Renderer::Component::Material::kMaxTextures + 1>
    descriptorSetLayoutBindings{
      {
        0,                                 // binding
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, // descriptorType
        1,                                 // descriptorCount
        VK_SHADER_STAGE_FRAGMENT_BIT,      // stageFlags
        nullptr                            // pImmutableSamplers
      } // This is the MaterialBuffer in gltf.frag
    };

  // Create renderable textures/views/samplers here so that the
  // descriptorSetLayout and descriptorSet are correct.

  if (!materials ||
      materials->size() < static_cast<std::size_t>(materialIndex)) {
    IRIS_LOG_LEAVE();
    return unexpected(std::system_error(
      Error::kFileParseFailed, "primitive references non-existent material"));
  }

  Renderer::Component::Material component;

  auto&& material = (*materials)[materialIndex];

  if (material.pbrMetallicRoughness) {
    if (material.pbrMetallicRoughness->baseColorTexture) {
      if (auto dt = CreateTexture(
            commandQueue, *material.pbrMetallicRoughness->baseColorTexture,
            imagesExtents, imagesBytes, true)) {
        shaderMacros.push_back("#define HAS_BASECOLOR_MAP");
        baseColorIndex = gsl::narrow_cast<int>(component.textures.size());

        component.textures.push_back(dt->texture);
        component.textureViews.push_back(dt->view);
        component.textureSamplers.push_back(dt->sampler);

        descriptorSetLayoutBindings.push_back({
          kBaseColorBinding,                         // binding
          VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, // descriptorType
          1,                                         // descriptorCount
          VK_SHADER_STAGE_FRAGMENT_BIT,              // stageFlags
          nullptr                                    // pImmutableSamplers
        });
      } else {
        IRIS_LOG_LEAVE();
        return unexpected(dt.error());
      }
    }

    if (material.pbrMetallicRoughness->metallicRoughnessTexture) {
      if (auto dt = CreateTexture(
            commandQueue,
            *material.pbrMetallicRoughness->metallicRoughnessTexture,
            imagesExtents, imagesBytes, false)) {
        shaderMacros.push_back("#define HAS_METALLICROUGHNESS_MAP");
        metallicRoughnessIndex =
          gsl::narrow_cast<int>(component.textures.size());

        component.textures.push_back(dt->texture);
        component.textureViews.push_back(dt->view);
        component.textureSamplers.push_back(dt->sampler);

        descriptorSetLayoutBindings.push_back({
          kMetallicRoughnessBinding,                 // binding
          VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, // descriptorType
          1,                                         // descriptorCount
          VK_SHADER_STAGE_FRAGMENT_BIT,              // stageFlags
          nullptr                                    // pImmutableSamplers
        });
      } else {
        IRIS_LOG_LEAVE();
        return unexpected(dt.error());
      }
    }
  }

  if (material.normalTexture) {
    // The NormalTextureInfo scale is handled down below
    TextureInfo ti;
    ti.index = material.normalTexture->index;
    ti.texCoord = material.normalTexture->texCoord;

    if (auto dt =
          CreateTexture(commandQueue, ti, imagesExtents, imagesBytes, false)) {
      shaderMacros.push_back("#define HAS_NORMAL_MAP");
      normalIndex = gsl::narrow_cast<int>(component.textures.size());

      component.textures.push_back(dt->texture);
      component.textureViews.push_back(dt->view);
      component.textureSamplers.push_back(dt->sampler);

      descriptorSetLayoutBindings.push_back({
        kNormalBinding,                            // binding
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, // descriptorType
        1,                                         // descriptorCount
        VK_SHADER_STAGE_FRAGMENT_BIT,              // stageFlags
        nullptr                                    // pImmutableSamplers
      });
    } else {
      IRIS_LOG_LEAVE();
      return unexpected(dt.error());
    }
  }

  if (material.emissiveTexture) {
    if (auto dt = CreateTexture(commandQueue, *material.emissiveTexture,
                                imagesExtents, imagesBytes, false)) {
      shaderMacros.push_back("#define HAS_EMISSIVE_MAP");
      emissiveIndex = gsl::narrow_cast<int>(component.textures.size());

      component.textures.push_back(dt->texture);
      component.textureViews.push_back(dt->view);
      component.textureSamplers.push_back(dt->sampler);

      descriptorSetLayoutBindings.push_back({
        kEmissiveBinding,                          // binding
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, // descriptorType
        1,                                         // descriptorCount
        VK_SHADER_STAGE_FRAGMENT_BIT,              // stageFlags
        nullptr                                    // pImmutableSamplers
      });
    } else {
      IRIS_LOG_LEAVE();
      return unexpected(dt.error());
    }
  }

  if (material.occlusionTexture) {
    // The OcclusionInfo strength is handled below
    TextureInfo ti;
    ti.index = material.occlusionTexture->index;
    ti.texCoord = material.occlusionTexture->texCoord;

    if (auto dt =
          CreateTexture(commandQueue, ti, imagesExtents, imagesBytes, false)) {
      shaderMacros.push_back("#define HAS_OCCLUSION_MAP");
      occlusionIndex = gsl::narrow_cast<int>(component.textures.size());

      component.textures.push_back(dt->texture);
      component.textureViews.push_back(dt->view);
      component.textureSamplers.push_back(dt->sampler);

      descriptorSetLayoutBindings.push_back({
        kOcclusionBinding,                         // binding
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, // descriptorType
        1,                                         // descriptorCount
        VK_SHADER_STAGE_FRAGMENT_BIT,              // stageFlags
        nullptr                                    // pImmutableSamplers
      });
    } else {
      IRIS_LOG_LEAVE();
      return unexpected(dt.error());
    }
  }

  VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI = {};
  descriptorSetLayoutCI.sType =
    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  descriptorSetLayoutCI.bindingCount =
    gsl::narrow_cast<std::uint32_t>(descriptorSetLayoutBindings.size());
  descriptorSetLayoutCI.pBindings = descriptorSetLayoutBindings.data();

  if (auto result =
        vkCreateDescriptorSetLayout(Renderer::sDevice, &descriptorSetLayoutCI,
                                    nullptr, &component.descriptorSetLayout);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return unexpected(std::system_error(make_error_code(result),
                                        "Cannot create descriptor set layout"));
  }

  Renderer::NameObject(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT,
                       component.descriptorSetLayout,
                       (meshName + ":DescriptorSetLayout").c_str());

  VkDescriptorSetAllocateInfo descriptorSetAI = {};
  descriptorSetAI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  descriptorSetAI.descriptorPool = Renderer::sDescriptorPool;
  descriptorSetAI.descriptorSetCount = 1;
  descriptorSetAI.pSetLayouts = &component.descriptorSetLayout;

  if (auto result = vkAllocateDescriptorSets(
        Renderer::sDevice, &descriptorSetAI, &component.descriptorSet);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return unexpected(std::system_error(make_error_code(result),
                                        "Cannot allocate descriptor set"));
  }

  Renderer::NameObject(VK_OBJECT_TYPE_DESCRIPTOR_SET, component.descriptorSet,
                       (meshName + ":DescriptorSet").c_str());

  absl::FixedArray<Shader> shaders(2);

  if (auto vs = LoadShaderFromFile("assets/shaders/gltf.vert",
                                   VK_SHADER_STAGE_VERTEX_BIT, shaderMacros)) {
    shaders[0] = *vs;
  } else {
    IRIS_LOG_LEAVE();
    return unexpected(vs.error());
  }

  if (auto fs =
        LoadShaderFromFile("assets/shaders/gltf_pbr.frag",
                           VK_SHADER_STAGE_FRAGMENT_BIT, shaderMacros)) {
    shaders[1] = *fs;
  } else {
    IRIS_LOG_LEAVE();
    return unexpected(fs.error());
  }

  VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI = {};
  inputAssemblyStateCI.sType =
    VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  inputAssemblyStateCI.topology = topology;

  VkPipelineViewportStateCreateInfo viewportStateCI = {};
  viewportStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewportStateCI.viewportCount = 1;
  viewportStateCI.scissorCount = 1;

  VkPipelineRasterizationStateCreateInfo rasterizationStateCI = {};
  rasterizationStateCI.sType =
    VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterizationStateCI.polygonMode = VK_POLYGON_MODE_FILL;
  rasterizationStateCI.cullMode = VK_CULL_MODE_BACK_BIT;
  rasterizationStateCI.frontFace = frontFace;
  rasterizationStateCI.lineWidth = 1.f;

  VkPipelineMultisampleStateCreateInfo multisampleStateCI = {};
  multisampleStateCI.sType =
    VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisampleStateCI.rasterizationSamples = Renderer::sSurfaceSampleCount;
  multisampleStateCI.minSampleShading = 1.f;

  VkPipelineDepthStencilStateCreateInfo depthStencilStateCI = {};
  depthStencilStateCI.sType =
    VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depthStencilStateCI.depthTestEnable = VK_TRUE;
  depthStencilStateCI.depthWriteEnable = VK_TRUE;
  depthStencilStateCI.depthCompareOp = VK_COMPARE_OP_LESS;

  absl::FixedArray<VkPipelineColorBlendAttachmentState>
    colorBlendAttachmentStates(1);
  colorBlendAttachmentStates[0] = {
    VK_FALSE,                            // blendEnable
    VK_BLEND_FACTOR_SRC_ALPHA,           // srcColorBlendFactor
    VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, // dstColorBlendFactor
    VK_BLEND_OP_ADD,                     // colorBlendOp
    VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, // srcAlphaBlendFactor
    VK_BLEND_FACTOR_ZERO,                // dstAlphaBlendFactor
    VK_BLEND_OP_ADD,                     // alphaBlendOp
    VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT // colorWriteMask
  };

  absl::FixedArray<VkDynamicState> dynamicStates{VK_DYNAMIC_STATE_VIEWPORT,
                                                 VK_DYNAMIC_STATE_SCISSOR};

  if (auto pipe = CreateRasterizationPipeline(
        shaders, vertexInputBindingDescriptions,
        vertexInputAttributeDescriptions, inputAssemblyStateCI, viewportStateCI,
        rasterizationStateCI, multisampleStateCI, depthStencilStateCI,
        colorBlendAttachmentStates, dynamicStates, 0,
        gsl::make_span(&component.descriptorSetLayout, 1))) {
    component.pipeline = *pipe;
  } else {
    IRIS_LOG_LEAVE();
    return unexpected(
      std::system_error(Error::kFileLoadFailed,
                        fmt::format("unable to create graphics pipeline: {}",
                                    pipe.error().what())));
  }

  auto staging =
    AllocateBuffer(sizeof(MaterialBuffer), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                   VMA_MEMORY_USAGE_CPU_TO_GPU);
  if (!staging) {
    IRIS_LOG_LEAVE();
    return unexpected(staging.error());
  }

  if (auto ptr = staging->Map<MaterialBuffer*>()) {
    // Set a default material first
    (*ptr)->MetallicRoughnessNormalOcclusion = glm::vec4(1.f, 1.f, 1.f, 1.f);
    (*ptr)->BaseColorFactor = glm::vec4(1.f);
    (*ptr)->EmissiveFactor = glm::vec3(0.f);

    if (material.pbrMetallicRoughness) {
      (*ptr)->MetallicRoughnessNormalOcclusion.x = gsl::narrow_cast<float>(
        material.pbrMetallicRoughness->metallicFactor.value_or(1.0));
      (*ptr)->MetallicRoughnessNormalOcclusion.y = gsl::narrow_cast<float>(
        material.pbrMetallicRoughness->roughnessFactor.value_or(1.0));
      (*ptr)->BaseColorFactor =
        material.pbrMetallicRoughness->baseColorFactor.value_or(glm::vec4(1.f));
    }

    if (material.normalTexture) {
      (*ptr)->MetallicRoughnessNormalOcclusion.z =
        gsl::narrow_cast<float>(material.normalTexture->scale.value_or(1.0));
    }

    if (material.occlusionTexture) {
      (*ptr)->MetallicRoughnessNormalOcclusion.w = gsl::narrow_cast<float>(
        material.occlusionTexture->strength.value_or(1.0));
    }

    (*ptr)->EmissiveFactor = material.emissiveFactor.value_or(glm::vec3(0.f));

    staging->Unmap();
  } else {
    IRIS_LOG_LEAVE();
    return unexpected(ptr.error());
  }

  if (auto buf = AllocateBuffer(sizeof(MaterialBuffer),
                                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
                                  VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                VMA_MEMORY_USAGE_GPU_ONLY)) {
    component.materialBuffer = *buf;
  } else {
    DestroyBuffer(*staging);
    IRIS_LOG_LEAVE();
    return unexpected(buf.error());
  }

  VkCommandBuffer commandBuffer;

  if (auto cb = Renderer::BeginOneTimeSubmit(commandQueue.commandPool)) {
    commandBuffer = *cb;
  } else {
    DestroyBuffer(component.materialBuffer);
    DestroyBuffer(*staging);
    IRIS_LOG_LEAVE();
    return unexpected(cb.error());
  }

  VkBufferCopy region = {};
  region.srcOffset = 0;
  region.dstOffset = 0;
  region.size = sizeof(MaterialBuffer);

  vkCmdCopyBuffer(commandBuffer, staging->buffer,
                  component.materialBuffer.buffer, 1, &region);

  if (auto result = Renderer::EndOneTimeSubmit(
        commandBuffer, commandQueue.commandPool, commandQueue.queue,
        commandQueue.submitFence);
      !result) {
    DestroyBuffer(component.materialBuffer);
    DestroyBuffer(*staging);
    IRIS_LOG_LEAVE();
    return unexpected(result.error());
  }

  VkDescriptorBufferInfo materialBufferInfo = {};
  materialBufferInfo.buffer = component.materialBuffer.buffer;
  materialBufferInfo.offset = 0;
  materialBufferInfo.range = VK_WHOLE_SIZE;

  absl::InlinedVector<VkWriteDescriptorSet,
                      Renderer::Component::Material::kMaxTextures + 1>
    writeDescriptorSets{{
      VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
      component.descriptorSet, // dstSet
      0,                       // dstBinding
      0,                       // dstArrayElement
      1,                       // descriptorCount
      VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
      nullptr,             // pImageInfo
      &materialBufferInfo, // pBufferInfo
      nullptr              // pTexelBufferView
    }};

  Expects(descriptorSetLayoutBindings.size() - 1 ==
          component.textureViews.size());
  Expects(component.textureViews.size() == component.textureSamplers.size());

  absl::FixedArray<VkDescriptorImageInfo> imageInfos(
    descriptorSetLayoutBindings.size() - 1);

  std::size_t const nBindings = descriptorSetLayoutBindings.size();
  for (std::size_t i = 1; i < nBindings; ++i) {
    imageInfos[i - 1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    if (descriptorSetLayoutBindings[i].binding == kBaseColorBinding) {
      imageInfos[i - 1].sampler = component.textureSamplers[baseColorIndex];
      imageInfos[i - 1].imageView = component.textureViews[baseColorIndex];
    } else if (descriptorSetLayoutBindings[i].binding == kNormalBinding) {
      imageInfos[i - 1].sampler = component.textureSamplers[normalIndex];
      imageInfos[i - 1].imageView = component.textureViews[normalIndex];
    } else if (descriptorSetLayoutBindings[i].binding == kEmissiveBinding) {
      imageInfos[i - 1].sampler = component.textureSamplers[emissiveIndex];
      imageInfos[i - 1].imageView = component.textureViews[emissiveIndex];
    } else if (descriptorSetLayoutBindings[i].binding ==
               kMetallicRoughnessBinding) {
      imageInfos[i - 1].sampler =
        component.textureSamplers[metallicRoughnessIndex];
      imageInfos[i - 1].imageView =
        component.textureViews[metallicRoughnessIndex];
    } else if (descriptorSetLayoutBindings[i].binding == kOcclusionBinding) {
      imageInfos[i - 1].sampler = component.textureSamplers[occlusionIndex];
      imageInfos[i - 1].imageView = component.textureViews[occlusionIndex];
    } else {
      IRIS_LOG_ERROR("Unknown binding: {}",
                     descriptorSetLayoutBindings[i].binding);
    }

    writeDescriptorSets.push_back({
      VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
      component.descriptorSet,                // dstSet
      descriptorSetLayoutBindings[i].binding, // dstBinding
      0,                                      // dstArrayElement
      1,                                      // descriptorCount
      VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      &imageInfos[i - 1], // pImageInfo
      nullptr,            // pBufferInfo
      nullptr             // pTexelBufferView
    });
  }

  vkUpdateDescriptorSets(
    Renderer::sDevice,
    gsl::narrow_cast<std::uint32_t>(writeDescriptorSets.size()),
    writeDescriptorSets.data(), 0, nullptr);

  IRIS_LOG_LEAVE();
  return component;
} // GLTF::CreateMaterial

} // namespace iris::gltf

namespace iris::io {

expected<void, std::system_error> static ParseGLTF(
  json const& j, std::filesystem::path const& path = "") noexcept {
  IRIS_LOG_ENTER();
  using namespace std::string_literals;

  std::filesystem::path const baseDir = path.parent_path();

  gltf::GLTF g;
  try {
    g = j.get<gltf::GLTF>();
  } catch (std::exception const& e) {
    IRIS_LOG_LEAVE();
    return unexpected(std::system_error(
      Error::kFileParseFailed, fmt::format("Parsing failed: {}", e.what())));
  }

  if (g.asset.version != "2.0") {
    if (g.asset.minVersion) {
      if (g.asset.minVersion != "2.0") {
        IRIS_LOG_LEAVE();
        return unexpected(
          std::system_error(Error::kFileParseFailed,
                            fmt::format("Unsupported version: {} / {}",
                                        g.asset.version, *g.asset.minVersion)));
      }
    } else {
      IRIS_LOG_LEAVE();
      return unexpected(std::system_error(
        Error::kFileParseFailed,
        fmt::format("Unsupported version: {} and no minVersion",
                    g.asset.version)));
    }
  }

  bool raytracing = false;
  if (g.extensionsRequired) {
    for (auto&& extension : *g.extensionsRequired) {
      IRIS_LOG_DEBUG("glTF extension: {}", extension);
      if (extension == "NIST_techniques_raytracing") raytracing = true;
    }
  }

  //
  // Read all the buffers into memory
  //
  auto&& buffers =
    g.buffers.value_or<decltype(gltf::GLTF::buffers)::value_type>({});
  std::vector<std::vector<std::byte>> buffersBytes;

  for (auto&& buffer : buffers) {
    if (buffer.uri) {
      std::filesystem::path uriPath(*buffer.uri);
      if (auto b =
            ReadFile(uriPath.is_relative() ? baseDir / uriPath : uriPath)) {
        buffersBytes.push_back(std::move(*b));
      } else {
        IRIS_LOG_LEAVE();
        b.error();
      }
    } else {
      IRIS_LOG_LEAVE();
      return unexpected(std::system_error(Error::kFileParseFailed,
                                          "unexpected buffer with no uri"));
    }
  }

  //
  // Read all the images into memory
  //
  auto&& images =
    g.images.value_or<decltype(gltf::GLTF::images)::value_type>({});
  std::vector<VkExtent2D> imagesExtents;
  std::vector<std::vector<std::byte>> imagesBytes;

  for (auto&& image : images) {
    if (image.uri) {
      std::filesystem::path uriPath(*image.uri);
      if (uriPath.is_relative()) {
        uriPath = kIRISContentDirectory / baseDir / uriPath;
      }
      IRIS_LOG_DEBUG("Reading {}", uriPath.string());

      int x, y, n;
      if (auto pixels = stbi_load(uriPath.string().c_str(), &x, &y, &n, 4);
          !pixels) {
        IRIS_LOG_LEAVE();
        return unexpected(
          std::system_error(Error::kFileNotSupported, stbi_failure_reason()));
      } else {
        imagesBytes.emplace_back(reinterpret_cast<std::byte*>(pixels),
                                 reinterpret_cast<std::byte*>(pixels) +
                                   x * y * 4);
        imagesExtents.push_back({gsl::narrow_cast<std::uint32_t>(x),
                                 gsl::narrow_cast<std::uint32_t>(y)});
      }
    } else if (image.bufferView) {
      imagesBytes.push_back(buffersBytes[*image.bufferView]);
    } else {
      IRIS_LOG_LEAVE();
      return unexpected(std::system_error(Error::kFileNotSupported,
                                          "image with no uri or bufferView"));
    }
  }

  //
  // Acquire a command queue
  //
  Renderer::CommandQueue commandQueue;
  if (auto q = Renderer::AcquireCommandQueue()) {
    commandQueue = *q;
  } else {
    IRIS_LOG_LEAVE();
    return unexpected(std::system_error(Error::kFileLoadFailed,
                                        "could not acquire command queue"));
  }

  if (!g.scene) {
    IRIS_LOG_DEBUG("no default scene specified; using first scene");
    g.scene = 0;
  }

  if (!g.scenes) {
    IRIS_LOG_DEBUG("no scenes; creating default scene with single node");
    gltf::Scene scene;
    scene.nodes = std::vector<int>{0};
    g.scenes->push_back(scene);
  }

  if (static_cast<std::size_t>(*g.scene) >= g.scenes->size()) {
    IRIS_LOG_LEAVE();
    return unexpected(std::system_error(
      Error::kFileLoadFailed, "default scene references non-existent scene"));
  }

  auto&& scene = (*g.scenes)[*g.scene];

  if (!scene.nodes) {
    IRIS_LOG_DEBUG("no nodes in scene; using first node");
    scene.nodes = std::vector<int>{0};
  }

  //
  // TODO: Parse the scene graph
  // TODO: this removes the hierarchy: need to maintain those relationships
  // TODO: implement Animatable(?) component to support animations
  //

  if (!raytracing) {
    std::vector<Renderer::Component::Renderable> renderables;

    for (auto&& node : *scene.nodes) {
      if (auto c = g.ParseNode<Renderer::Component::Renderable>(
            commandQueue, node, glm::mat4x4(1.f), path, buffersBytes,
            imagesExtents, imagesBytes)) {
        renderables.insert(renderables.end(), c->begin(), c->end());
      } else {
        IRIS_LOG_LEAVE();
      }
    }

    IRIS_LOG_DEBUG("Adding {} renderables", renderables.size());
    for (auto&& r : renderables) Renderer::AddRenderable(r);
  } else {
    Renderer::Component::Traceable traceable;

    /////
    //
    // Create the geometries
    //
    /////

    for (auto&& node : *scene.nodes) {
      if (auto c = g.ParseNode<Renderer::Component::Traceable::Geometry>(
            commandQueue, node, glm::mat4x4(1.f), path, buffersBytes,
            imagesExtents, imagesBytes)) {
        traceable.geometries.insert(traceable.geometries.end(), c->begin(),
                                    c->end());
      } else {
        IRIS_LOG_LEAVE();
        return unexpected(c.error());
      }
    }

    if (auto structure = CreateTopLevelAccelerationStructure(
          gsl::narrow_cast<std::uint32_t>(traceable.geometries.size()), 0)) {
      traceable.topLevelAccelerationStructure = *structure;
    } else {
      IRIS_LOG_LEAVE();
      return unexpected(structure.error());
    }

    /////
    //
    // Create the pipeline
    //
    /////

    if (auto p = g.ParseRaytracingPipeline(
          gsl::narrow_cast<int>(traceable.geometries.size()))) {
      std::tie(traceable.descriptorSetLayout, traceable.descriptorSet,
               traceable.shaderGroups, traceable.pipeline) = *p;
    } else {
      IRIS_LOG_LEAVE();
      return unexpected(p.error());
    }

    /////
    //
    // Parse the materials
    //
    /////

    if (auto m = g.ParseRaytracingMaterials(traceable.shaderGroups)) {
      // TODO
    } else {
      IRIS_LOG_LEAVE();
      return unexpected(m.error());
    }

    /////
    //
    // Create the Shader Binding Tables
    //
    /////

    auto&& shaderGroupHandleSize =
      vk::GetRayTracingProperties(Renderer::sPhysicalDevice)
        .shaderGroupHandleSize;

    auto shaderGroupHandles = vk::GetRayTracingShaderHandles(
      Renderer::sPhysicalDevice, Renderer::sDevice, traceable.pipeline.pipeline,
      gsl::narrow_cast<std::uint32_t>(traceable.shaderGroups.size()));
    if (!shaderGroupHandles) {
      IRIS_LOG_LEAVE();
      return unexpected(shaderGroupHandles.error());
    }

    if (auto buf =
          CreateBuffer(commandQueue.commandPool,           // commandPool
                       commandQueue.queue,                 // queue
                       commandQueue.submitFence,           // fence
                       VK_BUFFER_USAGE_RAY_TRACING_BIT_NV, // bufferUsage
                       VMA_MEMORY_USAGE_GPU_ONLY,          // memoryUsage
                       shaderGroupHandleSize,              // size
                       shaderGroupHandles->data()          // data
                       )) {
      traceable.raygenShaderBindingTable = *buf;
    } else {
      IRIS_LOG_LEAVE();
      return unexpected(buf.error());
    }

    traceable.missBindingStride = shaderGroupHandleSize;

    if (auto buf = CreateBuffer(
          commandQueue.commandPool,                          // commandPool
          commandQueue.queue,                                // queue
          commandQueue.submitFence,                          // fence
          VK_BUFFER_USAGE_RAY_TRACING_BIT_NV,                // bufferUsage
          VMA_MEMORY_USAGE_GPU_ONLY,                         // memoryUsage
          shaderGroupHandleSize,                             // size
          shaderGroupHandles->data() + shaderGroupHandleSize // data
          )) {
      traceable.missShadersBindingTable = *buf;
    } else {
      IRIS_LOG_LEAVE();
      return unexpected(buf.error());
    }

    traceable.hitBindingStride = shaderGroupHandleSize;

    if (auto buf = CreateBuffer(
          commandQueue.commandPool,           // commandPool
          commandQueue.queue,                 // queue
          commandQueue.submitFence,           // fence
          VK_BUFFER_USAGE_RAY_TRACING_BIT_NV, // bufferUsage
          VMA_MEMORY_USAGE_GPU_ONLY,          // memoryUsage
          shaderGroupHandleSize,              // size
          shaderGroupHandles->data() + (shaderGroupHandleSize * 2) // data
          )) {
      traceable.hitShadersBindingTable = *buf;
    } else {
      IRIS_LOG_LEAVE();
      return unexpected(buf.error());
    }

    if (auto img = AllocateImage(
          VK_FORMAT_R8G8B8A8_UNORM,    // format
          traceable.outputImageExtent, // extent
          1,                           // mipLevels
          1,                           // arrayLayers
          VK_SAMPLE_COUNT_1_BIT,       // sampleCount
          VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT, // imageUsage
          VK_IMAGE_TILING_OPTIMAL,           // imageTiling
          VMA_MEMORY_USAGE_GPU_ONLY          // memoryUsage
          )) {
      traceable.outputImage = *img;
    } else {
      IRIS_LOG_LEAVE();
      return unexpected(img.error());
    }

    if (auto view = CreateImageView(traceable.outputImage,    // image
                                    VK_IMAGE_VIEW_TYPE_2D,    // type
                                    VK_FORMAT_R8G8B8A8_UNORM, // format
                                    {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1})) {
      traceable.outputImageView = *view;
    } else {
      IRIS_LOG_LEAVE();
      return unexpected(view.error());
    }

    VkFenceCreateInfo traceFenceCI = {};
    traceFenceCI.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    traceFenceCI.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    if (auto result = vkCreateFence(Renderer::sDevice, &traceFenceCI, nullptr,
                                    &traceable.traceFinishedFence);
        result != VK_SUCCESS) {
      IRIS_LOG_LEAVE();
      return unexpected(
        std::system_error(make_error_code(result), "Cannot create fence"));
    }

    if (auto result =
          TransitionImage(commandQueue.commandPool, commandQueue.queue,
                          commandQueue.submitFence, traceable.outputImage,
                          VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1, 1);
        !result) {
      IRIS_LOG_LEAVE();
      return unexpected(result.error());
    }

    Renderer::SetTraceable(traceable);
  }

  IRIS_LOG_LEAVE();
  return {};
} // ParseGLTF

} // namespace iris::io

std::function<std::system_error(void)>
iris::io::LoadGLTF(std::filesystem::path const& path) noexcept {
  IRIS_LOG_ENTER();

  json j;
  if (auto&& bytes = ReadFile(path)) {
    try {
      j = json::parse(*bytes);
    } catch (std::exception const& e) {
      IRIS_LOG_LEAVE();
      IRIS_LOG_ERROR("Error parsing {}: {}", path.string(), e.what());
      return []() { return std::system_error(Error::kFileParseFailed); };
    }
  } else {
    IRIS_LOG_LEAVE();
    IRIS_LOG_ERROR("Error reading {}: {}", path.string(), bytes.error().what());
    return []() { return std::system_error(Error::kFileLoadFailed); };
  }

  if (auto ret = ParseGLTF(j, path); !ret) {
    IRIS_LOG_ERROR("Error parsing GLTF: {}", ret.error().what());
    return []() { return std::system_error(Error::kFileLoadFailed); };
  }

  IRIS_LOG_LEAVE();
  return []() { return std::system_error(Error::kNone); };
} // iris::io::LoadGLTF

std::function<std::system_error(void)>
iris::io::LoadGLTF(json const& gltf) noexcept {
  IRIS_LOG_ENTER();

  if (auto ret = ParseGLTF(gltf); !ret) {
    IRIS_LOG_ERROR("Error parsing GLTF: {}", ret.error().what());
    return []() { return std::system_error(Error::kFileLoadFailed); };
  }

  IRIS_LOG_LEAVE();
  return []() { return std::system_error(Error::kNone); };
} // iris::io::LoadGLTF
