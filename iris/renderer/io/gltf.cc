#include "renderer/io/gltf.h"
#include "renderer/buffer.h"
#include "error.h"
#include "gsl/gsl"
#include "logging.h"
#include "tao/json.hpp"
#include "tao/json/contrib/traits.hpp"

namespace iris::Renderer::io {

namespace gltf {

struct Buffer {
  int byteLength;
  std::optional<std::string> uri;
  std::optional<std::string> name;
  std::vector<std::byte> data;
}; // struct Buffer

struct BufferTraits
  : public tao::json::binding::object<
      TAO_JSON_BIND_REQUIRED("byteLength", &Buffer::byteLength),
      TAO_JSON_BIND_OPTIONAL("uri", &Buffer::uri),
      TAO_JSON_BIND_OPTIONAL("name", &Buffer::name)> {

  template <template <typename...> class Traits>
  static Buffer as(tao::json::basic_value<Traits> const& value) {
    auto const& object[[maybe_unused]] = value.get_object();
    Buffer buffer;

    buffer.byteLength = value.at("byteLength").template as<int>();
    buffer.uri = value.at("uri").template as<std::string>();
    buffer.name = value.at("name").template as<std::string>();

    return buffer;
  }
}; // struct BufferTraits

struct BufferView {
  int buffer;
  int byteLength;
  std::optional<int> byteOffset;
  std::optional<int> byteStride;
  std::optional<int> target;
  std::optional<std::string> name;
}; // struct BufferView

struct BufferViewTraits
  : public tao::json::binding::object<
      TAO_JSON_BIND_REQUIRED("buffer", &BufferView::buffer),
      TAO_JSON_BIND_REQUIRED("byteLength", &BufferView::byteLength),
      TAO_JSON_BIND_OPTIONAL("byteOffset", &BufferView::byteOffset),
      TAO_JSON_BIND_OPTIONAL("byteStride", &BufferView::byteStride),
      TAO_JSON_BIND_OPTIONAL("target", &BufferView::target),
      TAO_JSON_BIND_OPTIONAL("name", &BufferView::name)> {

  template <template <typename...> class Traits>
  static BufferView as(tao::json::basic_value<Traits> const& value) {
    auto const& object[[maybe_unused]] = value.get_object();
    BufferView view;

    view.buffer = value.at("buffer").template as<int>();
    view.byteLength = value.at("byteLength").template as<int>();
    view.byteOffset = value.at("byteOffset").template as<int>();
    view.byteStride = value.at("byteStride").template as<int>();
    view.target = value.at("target").template as<int>();
    view.name = value.at("name").template as<std::string>();

    if (!view.byteOffset) view.byteOffset = 0;

    return view;
  }
}; // struct BufferViewTraits

struct Accessor {
  int componentType;
  int count;
  std::string type;
  std::optional<int> bufferView;
  std::optional<int> byteOffset;
  std::optional<bool> normalized;
  std::optional<std::vector<double>> min;
  std::optional<std::vector<double>> max;
  std::optional<std::string> name;
}; // struct Accessor

struct AccessorTraits
  : public tao::json::binding::object<
      TAO_JSON_BIND_REQUIRED("componentType", &Accessor::componentType),
      TAO_JSON_BIND_REQUIRED("count", &Accessor::count),
      TAO_JSON_BIND_REQUIRED("type", &Accessor::type),
      TAO_JSON_BIND_OPTIONAL("bufferView", &Accessor::bufferView),
      TAO_JSON_BIND_OPTIONAL("byteOffset", &Accessor::byteOffset),
      TAO_JSON_BIND_OPTIONAL("normalized", &Accessor::normalized),
      TAO_JSON_BIND_OPTIONAL("min", &Accessor::min),
      TAO_JSON_BIND_OPTIONAL("max", &Accessor::max),
      TAO_JSON_BIND_OPTIONAL("name", &Accessor::name)> {

  template <template <typename...> class Traits>
  static Accessor as(tao::json::basic_value<Traits> const& value) {
    auto const& object[[maybe_unused]] = value.get_object();
    Accessor accessor;

    accessor.componentType = value.at("componentType").template as<int>();
    accessor.count = value.at("count").template as<int>();
    accessor.type = value.at("type").template as<std::string>();
    accessor.bufferView = value.at("bufferView").template as<int>();
    accessor.byteOffset = value.at("byteOffset").template as<int>();
    accessor.normalized = value.at("normalized").template as<bool>();
    accessor.min = value.at("min").template as<std::vector<double>>();
    accessor.max = value.at("max").template as<std::vector<double>>();
    accessor.name = value.at("name").template as<std::string>();

    if (!accessor.byteOffset) accessor.byteOffset = 0;
    if (!accessor.normalized) accessor.normalized = false;

    return accessor;
  }
}; // struct AccessorTraits

struct TextureInfo {
  int index;
  std::optional<int> texCoord;
}; // struct TextureInfo

struct TextureInfoTraits
  : public tao::json::binding::object<
      TAO_JSON_BIND_REQUIRED("index", &TextureInfo::index),
      TAO_JSON_BIND_OPTIONAL("texCoord", &TextureInfo::texCoord)> {

  template <template <typename...> class Traits>
  static TextureInfo as(tao::json::basic_value<Traits> const& value) {
    auto const& object[[maybe_unused]] = value.get_object();
    TextureInfo info;

    info.index = value.at("index").template as<int>();
    info.texCoord = value.at("texCoord").template as<int>();

    if (!info.texCoord) info.texCoord = 0;

    return info;
  }
}; // struct TextureInfoTraits

struct PBRMetallicRoughness {
  std::optional<std::vector<double>> baseColorFactor;
  std::optional<TextureInfo> baseColorTexture;
  std::optional<double> metallicFactor;
  std::optional<double> roughnessFactor;
  std::optional<TextureInfo> metallicRoughnessTexture;
}; // struct PBRMetallicRoughness

struct PBRMetallicRoughnessTraits
  : public tao::json::binding::object<
      TAO_JSON_BIND_OPTIONAL("baseColorFactor",
                             &PBRMetallicRoughness::baseColorFactor),
      TAO_JSON_BIND_OPTIONAL("baseColorTexture",
                             &PBRMetallicRoughness::baseColorTexture),
      TAO_JSON_BIND_OPTIONAL("metallicFactor",
                             &PBRMetallicRoughness::metallicFactor),
      TAO_JSON_BIND_OPTIONAL("roughnessFactor",
                             &PBRMetallicRoughness::roughnessFactor),
      TAO_JSON_BIND_OPTIONAL("metallicRoughnessTexture",
                             &PBRMetallicRoughness::metallicRoughnessTexture)> {

  template <template <typename...> class Traits>
  static PBRMetallicRoughness as(tao::json::basic_value<Traits> const& value) {
    auto const& object[[maybe_unused]] = value.get_object();
    PBRMetallicRoughness pbrMR;

    pbrMR.baseColorFactor =
      value.at("baseColorFactor").template as<std::vector<double>>();
    pbrMR.baseColorTexture =
      TextureInfoTraits::as(value.at("baseColorTexture"));
    pbrMR.metallicFactor = value.at("metallicFactor").template as<double>();
    pbrMR.roughnessFactor = value.at("roughnessFactor").template as<double>();
    pbrMR.metallicRoughnessTexture =
      TextureInfoTraits::as(value.at("metallicRoughnessTexture"));

    if (!pbrMR.baseColorFactor) {
      pbrMR.baseColorFactor = std::vector<double>{1, 1, 1, 1};
    }

    if (!pbrMR.metallicFactor) pbrMR.metallicFactor = 1;
    if (!pbrMR.roughnessFactor) pbrMR.roughnessFactor = 1;

    return pbrMR;
  }
}; // struct PBRMetallicRoughnessTraits

struct NormalTextureInfo {
  int index;
  std::optional<int> texCoord;
  std::optional<double> scale;
}; // struct NormalTextureInfo

struct NormalTextureInfoTraits
  : public tao::json::binding::object<
      TAO_JSON_BIND_REQUIRED("index", &NormalTextureInfo::index),
      TAO_JSON_BIND_OPTIONAL("texCoord", &NormalTextureInfo::texCoord),
      TAO_JSON_BIND_OPTIONAL("scale", &NormalTextureInfo::scale)> {

  template <template <typename...> class Traits>
  static NormalTextureInfo as(tao::json::basic_value<Traits> const& value) {
    auto const& object[[maybe_unused]] = value.get_object();
    NormalTextureInfo info;

    info.index = value.at("index").template as<int>();
    info.texCoord = value.at("texCoord").template as<int>();
    info.scale = value.at("scale").template as<double>();

    if (!info.texCoord) info.texCoord = 0;
    if (!info.scale) info.scale = 1;

    return info;
  }
}; // struct NormalTextureInfoTraits

struct OcclusionTextureInfo {
  int index;
  std::optional<int> texCoord;
  std::optional<double> strength;
}; // struct OcclusionTextureInfo

struct OcclusionTextureInfoTraits
  : public tao::json::binding::object<
      TAO_JSON_BIND_REQUIRED("index", &OcclusionTextureInfo::index),
      TAO_JSON_BIND_OPTIONAL("texCoord", &OcclusionTextureInfo::texCoord),
      TAO_JSON_BIND_OPTIONAL("strength", &OcclusionTextureInfo::strength)> {

  template <template <typename...> class Traits>
  static OcclusionTextureInfo as(tao::json::basic_value<Traits> const& value) {
    auto const& object[[maybe_unused]] = value.get_object();
    OcclusionTextureInfo info;

    info.index = value.at("index").template as<int>();
    info.texCoord = value.at("texCoord").template as<int>();
    info.strength = value.at("strength").template as<double>();

    if (!info.texCoord) info.texCoord = 0;
    if (!info.strength) info.strength = 1;

    return info;
  }
}; // struct OcclusionTextureInfoTraits

struct Material {
  std::optional<PBRMetallicRoughness> pbrMetallicRoughness;
  std::optional<NormalTextureInfo> normalTexture;
  std::optional<OcclusionTextureInfo> occlusionTexture;
  std::optional<TextureInfo> emissiveTexture;
  std::optional<std::vector<double>> emissiveFactor;
  std::optional<std::string> alphaMode;
  std::optional<double> alphaCutoff;
  std::optional<bool> doubleSided;
  std::optional<std::string> name;
}; // struct Material

struct MaterialTraits
  : public tao::json::binding::object<
      TAO_JSON_BIND_OPTIONAL("pbrMetallicRoughness",
                             &Material::pbrMetallicRoughness),
      TAO_JSON_BIND_OPTIONAL("normalTexture", &Material::normalTexture),
      TAO_JSON_BIND_OPTIONAL("occlusionTexture", &Material::occlusionTexture),
      TAO_JSON_BIND_OPTIONAL("emissiveTexture", &Material::emissiveTexture),
      TAO_JSON_BIND_OPTIONAL("emissiveFactor", &Material::emissiveFactor),
      TAO_JSON_BIND_OPTIONAL("alphaMode", &Material::alphaMode),
      TAO_JSON_BIND_OPTIONAL("alphaCutoff", &Material::alphaCutoff),
      TAO_JSON_BIND_OPTIONAL("doubleSided", &Material::doubleSided),
      TAO_JSON_BIND_OPTIONAL("name", &Material::name)> {

  template <template <typename...> class Traits>
  static Material as(tao::json::basic_value<Traits> const& value) {
    auto const& object[[maybe_unused]] = value.get_object();
    Material material;

    material.pbrMetallicRoughness =
      PBRMetallicRoughnessTraits::as(value.at("pbrMetallicRoughness"));
    material.normalTexture =
      NormalTextureInfoTraits::as(value.at("normalTexture"));
    material.occlusionTexture =
      OcclusionTextureInfoTraits::as(value.at("occlusionTexture"));
    material.emissiveTexture =
      TextureInfoTraits::as(value.at("emissiveTexture"));

    material.emissiveFactor =
      value.at("emissiveFactor").template as<std::vector<double>>();
    material.alphaMode = value.at("alphaMode").template as<std::string>();
    material.alphaCutoff = value.at("alphaCutoff").template as<double>();
    material.doubleSided = value.at("doubleSided").template as<bool>();
    material.name = value.at("name").template as<std::string>();

    if (!material.emissiveFactor) {
      material.emissiveFactor = std::vector<double>{0, 0, 0};
    }
    if (!material.alphaMode) material.alphaMode = "OPAQUE";
    if (!material.alphaCutoff) material.alphaCutoff = 0.5;
    if (!material.doubleSided) material.doubleSided =false;

    return material;
  }
}; // struct MaterialTraits

struct Primitive {
  std::map<std::string, int> attributes;
  std::optional<int> indices;
  std::optional<int> material;
  std::optional<int> mode;
  std::optional<std::vector<int>> targets;
}; // struct Primitive

struct PrimitiveTraits
  : public tao::json::binding::object<
      TAO_JSON_BIND_REQUIRED("attributes", &Primitive::attributes),
      TAO_JSON_BIND_OPTIONAL("indices", &Primitive::indices),
      TAO_JSON_BIND_OPTIONAL("material", &Primitive::material),
      TAO_JSON_BIND_OPTIONAL("mode", &Primitive::mode),
      TAO_JSON_BIND_OPTIONAL("targets", &Primitive::targets)> {

  template <template <typename...> class Traits>
  static Primitive as(tao::json::basic_value<Traits> const& value) {
    auto const& object[[maybe_unused]] = value.get_object();
    Primitive primitive;

    primitive.attributes =
      value.at("attributes").template as<std::map<std::string, int>>();

    primitive.indices = value.at("indices").template as<int>();
    primitive.material = value.at("material").template as<int>();
    primitive.mode = value.at("mode").template as<int>();
    primitive.targets = value.at("targets").template as<std::vector<int>>();

    if (!primitive.mode) primitive.mode = 4;

    return primitive;
  }
}; // struct PrimitiveTraits

struct Mesh {
  std::vector<Primitive> primitives;
  std::optional<std::vector<double>> weights;
  std::optional<std::string> name;
}; // struct Mesh

struct MeshTraits
  : public tao::json::binding::object<
      TAO_JSON_BIND_REQUIRED("primitives", &Mesh::primitives),
      TAO_JSON_BIND_OPTIONAL("weights", &Mesh::weights),
      TAO_JSON_BIND_OPTIONAL("name", &Mesh::name)> {

  template <template <typename...> class Traits>
  static Mesh as(tao::json::basic_value<Traits> const& value) {
    auto const& object[[maybe_unused]] = value.get_object();
    Mesh mesh;

    for (auto&& primitiveValue : value.at("primitives").get_array()) { // get_array makes a copy...
      mesh.primitives.push_back(PrimitiveTraits::as(primitiveValue));
    }
    mesh.weights = value.at("weights").template as<std::vector<double>>();
    mesh.name = value.at("name").template as<std::string>();

    return mesh;
  }
}; // struct MeshTraits

} // namespace gltf

tl::expected<bool, std::system_error> static CheckVersion(
  tao::json::value const* asset) noexcept {
  if (!asset) {
    return tl::unexpected(
      std::system_error(Error::kFileParseFailed, "No asset tag"));
  }

  if (auto version = asset->find("version")) {
    if (version->is_string_type()) {
      if (version->as<std::string>() == "2.0") {
        return true;
      } else {
        if (auto minVersion = asset->find("minVersion")) {
          if (minVersion->is_string_type()) {
            return (minVersion->as<std::string>() == "2.0");
          } else {
            return tl::unexpected(std::system_error(
              Error::kFileParseFailed, "Bad minVersion field in asset tag"));
          }
        } else {
          return false;
        }
      }
    } else {
      return tl::unexpected(std::system_error(
        Error::kFileParseFailed, "Bad version field in asset tag"));
    }
  } else {
    return tl::unexpected(std::system_error(Error::kFileParseFailed,
                                            "No version field in asset tag"));
  }
} // CheckVersion

tl::expected<std::vector<gltf::Buffer>, std::exception>
LoadBuffers(tao::json::value const* buffersValue,
            filesystem::path const& baseDir) noexcept {
  if (!buffersValue) return {};

  if (!buffersValue->is_array()) {
    return tl::unexpected(
      std::system_error(Error::kFileParseFailed, "buffers is not an array"));
  }

  std::vector<gltf::Buffer> buffers;

  for (auto&& value : buffersValue->get_array()) { // get_array makes a copy...
    try {
      auto&& buffer = gltf::BufferTraits::as(value);
      //auto&& buffer = value.as<gltf::Buffer>();

    if (buffer.uri) {
        filesystem::path uriPath(*buffer.uri);
        if (uriPath.is_relative()) uriPath = baseDir / uriPath;

        if (auto data = io::ReadFile(uriPath)) {
          buffer.data = std::move(*data);
        } else {
          return tl::unexpected(data.error());
        }
      }

      buffers.push_back(buffer);
    } catch (std::exception const& e) { return tl::unexpected(e); }
  }

  return buffers;
} // LoadBuffers

tl::expected<std::vector<gltf::BufferView>, std::exception>
LoadBufferViews(tao::json::value const* bufferViewsValue) noexcept {
  if (!bufferViewsValue) return {};

  if (!bufferViewsValue->is_array()) {
    return tl::unexpected(
      std::system_error(Error::kFileParseFailed, "bufferViews is not an array"));
  }

  std::vector<gltf::BufferView> bufferViews;

  for (auto&& value : bufferViewsValue->get_array()) { // get_array makes a copy...
    try {
      bufferViews.push_back(gltf::BufferViewTraits::as(value));
    } catch (std::exception const& e) {
      return tl::unexpected(e);
    }
  }

  return bufferViews;
} // LoadBuffers

tl::expected<std::vector<gltf::Accessor>, std::exception>
LoadAccessors(tao::json::value const* accessorsValue) noexcept {
  if (!accessorsValue) return {};

  if (!accessorsValue->is_array()) {
    return tl::unexpected(
      std::system_error(Error::kFileParseFailed, "accessors is not an array"));
  }

  std::vector<gltf::Accessor> accessors;

  for (auto&& value : accessorsValue->get_array()) { // get_array makes a copy...
    try {
      accessors.push_back(gltf::AccessorTraits::as(value));
    } catch (std::exception const& e) {
      return tl::unexpected(e);
    }
  }

  return accessors;
} // LoadAccessors

tl::expected<std::vector<gltf::Material>, std::exception>
LoadMaterials(tao::json::value const* materialsValue) noexcept {
  if (!materialsValue) return {};

  if (!materialsValue->is_array()) {
    return tl::unexpected(
      std::system_error(Error::kFileParseFailed, "materials is not an array"));
  }

  std::vector<gltf::Material> materials;

  for (auto&& value : materialsValue->get_array()) { // get_array makes a copy...
    try {
      materials.push_back(gltf::MaterialTraits::as(value));
    } catch (std::exception const& e) {
      return tl::unexpected(e);
    }
  }

  return materials;
} // LoadMaterials

tl::expected<std::vector<gltf::Mesh>, std::exception>
LoadMeshes(tao::json::value const* meshesValue) noexcept {
  if (!meshesValue) return {};

  if (!meshesValue->is_array()) {
    return tl::unexpected(
      std::system_error(Error::kFileParseFailed, "meshes is not an array"));
  }

  std::vector<gltf::Mesh> meshes;

  for (auto&& value : meshesValue->get_array()) { // get_array makes a copy...
    try {
      meshes.push_back(gltf::MeshTraits::as(value));
    } catch (std::exception const& e) {
      return tl::unexpected(e);
    }
  }

  return meshes;
} // LoadMeshes

} // namespace iris::Renderer::io

tl::expected<std::function<void(void)>, std::system_error>
iris::Renderer::io::LoadGLTF(filesystem::path const& path) noexcept {
  using namespace std::string_literals;
  IRIS_LOG_ENTER();
  filesystem::path const baseDir = path.parent_path();

  std::string json;
  if (auto&& bytes = ReadFile(path)) {
    json =
      std::string(reinterpret_cast<char const*>(bytes->data()), bytes->size());
  } else {
    return tl::unexpected(bytes.error());
  }

  tao::json::value const gltf = tao::json::from_string(json);
  if (!gltf) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(Error::kFileParseFailed, "No GLTF?"));
  }

  if (auto vg = CheckVersion(gltf.find("asset")); !vg) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(Error::kFileParseFailed, "Unsupported version"));
  }

  std::vector<gltf::Buffer> gltfBuffers;
  if (auto b = LoadBuffers(gltf.find("buffers"), baseDir)) {
    gltfBuffers = std::move(*b);
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(
      Error::kFileParseFailed, "Reading buffers: "s + b.error().what()));
  }

  std::vector<gltf::BufferView> bufferViews;
  if (auto v = LoadBufferViews(gltf.find("bufferViews"))) {
    bufferViews = std::move(*v);
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(
      Error::kFileParseFailed, "Reading bufferViews: "s + v.error().what()));
  }

  std::vector<gltf::Accessor> accessors;
  if (auto a = LoadAccessors(gltf.find("accessors"))) {
    accessors = std::move(*a);
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(
      Error::kFileParseFailed, "Reading accessors: "s + a.error().what()));
  }

  std::vector<gltf::Material> materials;
  if (auto m = LoadMaterials(gltf.find("materials"))) {
    materials = std::move(*m);
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(
      Error::kFileParseFailed, "Reading materials: "s + m.error().what()));
  }

  std::vector<gltf::Mesh> meshes;
  if (auto m = LoadMeshes(gltf.find("meshes"))) {
    meshes = std::move(*m);
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(
      Error::kFileParseFailed, "Reading meshes: "s + m.error().what()));
  }

  std::vector<Buffer> gpuBuffers;
  for (auto view : bufferViews) {
    VkBufferUsageFlagBits bufferUsage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (view.target && *view.target == 34963) {
      bufferUsage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    } else {
      GetLogger()->warn("no bufferView target; assuming vertex buffer");
    }

    if (auto b = Buffer::CreateFromMemory(
          view.byteLength, bufferUsage, VMA_MEMORY_USAGE_GPU_ONLY,
          gsl::not_null(gltfBuffers[view.buffer].data.data() +
                        *view.byteOffset),
          view.name ? *view.name : "")) {
      gpuBuffers.push_back(std::move(*b));
    } else {
      IRIS_LOG_LEAVE();
      return tl::unexpected(b.error());
    }
  }

  GetLogger()->error("GLTF loading not implemented yet: {}", path.string());
  IRIS_LOG_LEAVE();
  return tl::unexpected(
    std::system_error(Error::kFileNotSupported, path.string()));
} // iris::Renderer::io::LoadGLTF

