#include "io/savg.h"
#include "config.h"

#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_split.h"
#include "absl/strings/strip.h"
#include "components/renderable.h"
#include "error.h"
#include "expected.hpp"
#include "glm/vec3.hpp"
#include "glm/vec4.hpp"
#include "glm/gtc/type_ptr.hpp"
#include "io/read_file.h"
#include "logging.h"
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
#include <algorithm>
#include <string>
#include <variant>
#include <vector>

namespace iris::savg {

struct Primitive {
  Primitive() = default;
  explicit Primitive(glm::vec4 c)
    : primColor(std::move(c)) {}
  virtual ~Primitive() noexcept {}

  std::optional<glm::vec4> primColor{};

  std::vector<glm::vec3> positions{};
  std::vector<glm::vec4> colors{};
  std::vector<glm::vec3> normals{};
}; // struct Primitive

struct Tristrips final : public Primitive {
  Tristrips() = default;
  explicit Tristrips(glm::vec4 c)
    : Primitive(std::move(c)) {}
}; // struct Tristrips

struct Lines final : public Primitive {
  Lines() = default;
  Lines(glm::vec4 c)
    : Primitive(std::move(c)) {}
}; // struct Lines

struct Points final : public Primitive {
  Points() = default;
  Points(glm::vec4 c)
    : Primitive(std::move(c)) {}
}; // struct Points

struct Shape {
}; // struct Shape

using State = std::variant<std::monostate, Tristrips, Lines, Points, Shape>;

template <typename T, typename It>
tl::expected<T, std::system_error> ParseVec(It start) noexcept {
  T vec;

  for (int i = 0; i < T::length(); ++i, ++start) {
    if (!absl::SimpleAtof(*start, &vec[i])) {
      return tl::unexpected(std::system_error(
        Error::kFileParseFailed, "Invalid value for primitive color"));
    }
  }

  return vec;
} // ParseVec

template <class T>
static T Start(std::vector<std::string_view> const& tokens) noexcept {
  if (tokens.size() == 5) {
    if (auto color = ParseVec<glm::vec4>(tokens.begin() + 1)) {
      return T(*color);
    } else {
      IRIS_LOG_WARN("Error parsing primitive color: {}; ignoring",
                    color.error().what());
      return T();
    }
  } else {
    if (tokens.size() != 1) {
      IRIS_LOG_WARN("Wrong number of values for primitive color: {}; ignoring",
                    tokens.size() - 1);
    }
    return T();
  }
} // Start

void ParseData(std::monostate, std::vector<std::string_view> const&) noexcept {}

tl::expected<std::optional<Renderer::Component::Renderable>, std::system_error>
End(std::monostate&) noexcept {
  return std::nullopt;
}

void ParseData(Primitive& primitive,
               std::vector<std::string_view> const& tokens) noexcept {
  if (tokens.size() != 3 && tokens.size() != 6 && tokens.size() != 7 &&
      tokens.size() != 10) {
    IRIS_LOG_WARN("Wrong number of values for primitive data: {}; ignoring",
                  tokens.size());
    return;
  }

  auto first = tokens.begin();
  if (auto position = ParseVec<glm::vec3>(first)) {
    primitive.positions.push_back(*position);
    first += glm::vec3::length();
  } else {
    IRIS_LOG_WARN("Error parsing xyz for primitive data: {}; ignoring",
                  position.error().what());
  }

  switch (tokens.size()) {
  case 6:
    if (auto normal = ParseVec<glm::vec3>(first)) {
      primitive.normals.push_back(*normal);
    } else {
      IRIS_LOG_WARN("Error parsing xnynzn for primitive data: {}; ignoring",
                    normal.error().what());
    }
    break;
  case 7:
    if (auto color = ParseVec<glm::vec4>(first)) {
      primitive.colors.push_back(*color);
    } else {
      IRIS_LOG_WARN("Error parsing rgba for primitive data: {}; ignoring",
                    color.error().what());
    }
    break;
  case 10:
    if (auto color = ParseVec<glm::vec4>(first)) {
      primitive.colors.push_back(*color);
      first += glm::vec4::length();
    } else {
      IRIS_LOG_WARN("Error parsing rgba for primitive data: {}; ignoring",
                    color.error().what());
      return;
    }

    if (auto normal = ParseVec<glm::vec3>(first)) {
      primitive.normals.push_back(*normal);
    } else {
      IRIS_LOG_WARN("Error parsing xnynzn for primitive data: {}; ignoring",
                    normal.error().what());
    }
    break;
  }
} // ParseData

tl::expected<Renderer::Component::Renderable, std::system_error>
CreateRenderable(Primitive const* primitive, VkPrimitiveTopology topology) noexcept {
  IRIS_LOG_ENTER();

  Renderer::CommandQueue commandQueue;
  if (auto cq = Renderer::AcquireCommandQueue()) {
    commandQueue = std::move(*cq);
  } else {
    return tl::unexpected(cq.error());
  }

  Renderer::Component::Renderable renderable;
  absl::InlinedVector<std::string, 8> shaderMacros;

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

  if (!primitive->colors.empty() && !primitive->normals.empty()) {
    if ((primitive->colors.size() != primitive->normals.size()) ||
        (primitive->normals.size() != primitive->positions.size())) {
      return tl::unexpected(std::system_error(
        Error::kFileParseFailed,
        "invalid data: unequal number of colors, normals, and positions"));
    }

    vertexInputAttributeDescriptions.push_back({
      1,                          // location
      0,                          // binding
      VK_FORMAT_R32G32B32_SFLOAT, // format
      vertexStride                // offset
    });
    vertexStride += sizeof(glm::vec3);
    shaderMacros.push_back("#define HAS_NORMALS");

    vertexInputAttributeDescriptions.push_back({
      2,                             // location
      0,                             // binding
      VK_FORMAT_R32G32B32A32_SFLOAT, // format
      vertexStride                   // offset
    });
    vertexStride += sizeof(glm::vec4);
    shaderMacros.push_back("#define HAS_COLORS");
  } else if (!primitive->colors.empty()) {
    if (primitive->colors.size() != primitive->positions.size()) {
      return tl::unexpected(std::system_error(
        Error::kFileParseFailed,
        "invalid data: unequal number of colors and positions"));
    }

    vertexInputAttributeDescriptions.push_back({
      2,                             // location
      0,                             // binding
      VK_FORMAT_R32G32B32A32_SFLOAT, // format
      vertexStride                   // offset
    });
    vertexStride += sizeof(glm::vec4);
    shaderMacros.push_back("#define HAS_COLORS");
  } else if (!primitive->normals.empty()) {
    if (primitive->colors.size() != primitive->positions.size()) {
      return tl::unexpected(std::system_error(
        Error::kFileParseFailed,
        "invalid data: unequal number of colors and positions"));
    }

    vertexInputAttributeDescriptions.push_back({
      1,                          // location
      0,                          // binding
      VK_FORMAT_R32G32B32_SFLOAT, // format
      vertexStride                // offset
    });
    vertexStride += sizeof(glm::vec3);
    shaderMacros.push_back("#define HAS_NORMALS");
  } else if (primitive->primColor) {
    vertexInputAttributeDescriptions.push_back({
      2,                             // location
      0,                             // binding
      VK_FORMAT_R32G32B32A32_SFLOAT, // format
      vertexStride                   // offset
    });
    vertexStride += sizeof(glm::vec4);
    shaderMacros.push_back("#define HAS_COLORS");
  }

  decltype(Renderer::Component::Material::vertexInputBindingDescriptions)
    vertexInputBindingDescriptions{{
      0,                          // binding
      vertexStride,               // stride
      VK_VERTEX_INPUT_RATE_VERTEX // inputRate
    }};

  VkDeviceSize const vertexBufferSize =
    vertexStride * primitive->positions.size();

  auto staging =
    AllocateBuffer(vertexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                   VMA_MEMORY_USAGE_CPU_TO_GPU);
  if (!staging) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(staging.error());
  }

  float* pVertexBuffer;
  if (auto ptr = staging->Map<float*>()) {
    pVertexBuffer = *ptr;
  } else {
    DestroyBuffer(*staging);
    IRIS_LOG_LEAVE();
    return tl::unexpected(ptr.error());
  }

  for (std::size_t i = 0; i < primitive->positions.size(); i++) {
    *pVertexBuffer++ = primitive->positions[i].x;
    *pVertexBuffer++ = primitive->positions[i].y;
    *pVertexBuffer++ = primitive->positions[i].z;

    if (!primitive->colors.empty()) {
      *pVertexBuffer++ = primitive->colors[i].r;
      *pVertexBuffer++ = primitive->colors[i].g;
      *pVertexBuffer++ = primitive->colors[i].b;
      *pVertexBuffer++ = primitive->colors[i].a;
    } else if (primitive->primColor) {
      *pVertexBuffer++ = primitive->primColor->r;
      *pVertexBuffer++ = primitive->primColor->g;
      *pVertexBuffer++ = primitive->primColor->b;
      *pVertexBuffer++ = primitive->primColor->a;
    }

    if (!primitive->normals.empty()) {
      *pVertexBuffer++ = primitive->normals[i].x;
      *pVertexBuffer++ = primitive->normals[i].y;
      *pVertexBuffer++ = primitive->normals[i].z;
    }
  }

  staging->Unmap();

  if (auto buf = AllocateBuffer(vertexBufferSize,
                                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                                  VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                VMA_MEMORY_USAGE_GPU_ONLY)) {
    renderable.vertexBuffer = std::move(*buf);
  } else {
    DestroyBuffer(*staging);
    IRIS_LOG_LEAVE();
    return tl::unexpected(buf.error());
  }

  VkCommandBuffer commandBuffer;
  if (auto cb = Renderer::BeginOneTimeSubmit(commandQueue.commandPool)) {
    commandBuffer = *cb;
  } else {
    DestroyBuffer(renderable.vertexBuffer);
    DestroyBuffer(*staging);
    IRIS_LOG_LEAVE();
    return tl::unexpected(cb.error());
  }

  VkBufferCopy region = {};
  region.srcOffset = 0;
  region.dstOffset = 0;
  region.size = vertexBufferSize;

  vkCmdCopyBuffer(commandBuffer, staging->buffer, renderable.vertexBuffer.buffer,
                  1, &region);

  if (auto result = Renderer::EndOneTimeSubmit(
        commandBuffer, commandQueue.commandPool, commandQueue.queue,
        commandQueue.submitFence);
      !result) {
    DestroyBuffer(renderable.vertexBuffer);
    DestroyBuffer(*staging);
    IRIS_LOG_LEAVE();
    return tl::unexpected(result.error());
  }

  renderable.numVertices =
    gsl::narrow_cast<std::uint32_t>(primitive->positions.size());
  renderable.modelMatrix = glm::mat4(1.f);

  // Compute the bounding sphere
  struct CoordAccessor {
    using Pit = decltype(primitive->positions)::const_iterator;
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

  Miniball::Miniball<CoordAccessor> mb(3, primitive->positions.begin(),
                                       primitive->positions.end());

  renderable.boundingSphere =
    glm::vec4(mb.center()[0], mb.center()[1], mb.center()[2],
              std::sqrt(mb.squared_radius()));
  IRIS_LOG_DEBUG("boundingSphere: ({} {} {}), {}", renderable.boundingSphere.x,
                 renderable.boundingSphere.y, renderable.boundingSphere.z,
                 renderable.boundingSphere.w);

#if PLATFORM_COMPILER_MSVC
#pragma warning(pop)
#elif PLATFORM_COMPILER_GCC
#pragma GCC diagnostic pop
#endif

  Renderer::Component::Material material;

  absl::FixedArray<Shader> shaders(2);

  if (auto vs = LoadShaderFromFile("assets/shaders/savg.vert",
                                   VK_SHADER_STAGE_VERTEX_BIT, shaderMacros)) {
    shaders[0] = std::move(*vs);
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(vs.error());
  }

  if (auto fs =
        LoadShaderFromFile("assets/shaders/savg.frag",
                           VK_SHADER_STAGE_FRAGMENT_BIT, shaderMacros)) {
    shaders[1] = std::move(*fs);
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(fs.error());
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
  rasterizationStateCI.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
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
    colorBlendAttachmentStates, dynamicStates, 0, {})) {
    material.pipeline = std::move(*pipe);
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(iris::Error::kFileLoadFailed,
                        fmt::format("unable to create graphics pipeline: {}",
                                    pipe.error().what())));
  }

  renderable.material = Renderer::AddMaterial(std::move(material));

  Renderer::ReleaseCommandQueue(commandQueue);
  IRIS_LOG_LEAVE();
  return renderable;
} // CreateRenderable

tl::expected<std::optional<Renderer::Component::Renderable>, std::system_error>
End(Tristrips&) noexcept {
  IRIS_LOG_ENTER();

  IRIS_LOG_LEAVE();
  return std::nullopt;
}

tl::expected<std::optional<Renderer::Component::Renderable>, std::system_error>
End(Lines& lines) noexcept {
  IRIS_LOG_ENTER();

  Renderer::Component::Renderable component;
  if (auto c = CreateRenderable(&lines, VK_PRIMITIVE_TOPOLOGY_LINE_LIST)) {
    component = std::move(*c);
  } else {
    return tl::unexpected(c.error());
  }

  IRIS_LOG_LEAVE();
  return component;
} // End

tl::expected<std::optional<Renderer::Component::Renderable>, std::system_error>
End(Points&) noexcept {
  IRIS_LOG_ENTER();

  IRIS_LOG_LEAVE();
  return std::nullopt;
}

void ParseData(Shape&, std::vector<std::string_view> const&) noexcept {}

tl::expected<std::optional<Renderer::Component::Renderable>, std::system_error>
End(Shape&) noexcept {
  IRIS_LOG_ENTER();

  IRIS_LOG_LEAVE();
  return std::nullopt;
}

tl::expected<std::optional<Renderer::Component::Renderable>, std::system_error>
ParseLine(State& state, std::string_view line) noexcept {
  using namespace std::string_literals;
  IRIS_LOG_ENTER();

  IRIS_LOG_TRACE("line: [{}]", line);
  std::vector<std::string_view> tokens =
    absl::StrSplit(line, " ", absl::SkipWhitespace());

  if (tokens.empty() || tokens[0].empty() || tokens[0][0] == '#') {
    IRIS_LOG_LEAVE();
    return std::nullopt;
  }

  tl::expected<std::optional<Renderer::Component::Renderable>,
               std::system_error>
    renderable{};

  if (auto nextState = std::visit(
        [&renderable, &tokens](auto&& currState) -> std::optional<State> {
          if (absl::StartsWithIgnoreCase(tokens[0], "END")) {
            renderable = End(currState);
            return State{};
          } else if (absl::StartsWithIgnoreCase(tokens[0], "TRI")) {
            renderable = End(currState);
            return Start<Tristrips>(tokens);
          } else if (absl::StartsWithIgnoreCase(tokens[0], "LIN")) {
            renderable = End(currState);
            return Start<Lines>(tokens);
          } else if (absl::StartsWithIgnoreCase(tokens[0], "POI")) {
            renderable = End(currState);
            return Start<Points>(tokens);
          }

          ParseData(currState, tokens);
          return std::nullopt;
        },
        state)) {
    state = *nextState;
  }

  IRIS_LOG_LEAVE();
  return renderable;
} // ParseLine

} // namespace iris::savg

namespace iris::io {

tl::expected<void, std::system_error> static ParseSAVG(
  std::vector<std::byte> const& bytes,
  std::filesystem::path const& path [[maybe_unused]] = "") noexcept {
  IRIS_LOG_ENTER();

  savg::State state;

  std::size_t const nBytes = bytes.size();
  for (std::size_t prev = 0, curr = 0; prev < nBytes; ++curr) {
    if (bytes[curr] == std::byte('\n') || curr == nBytes - 1) {
      std::string line(reinterpret_cast<char const*>(&bytes[prev]),
                       reinterpret_cast<char const*>(&bytes[curr]));
      if (auto possibleRenderable = ParseLine(state, line)) {
        if (*possibleRenderable) {
          Renderer::AddRenderable(std::move(**possibleRenderable));
        }
      } else {
        IRIS_LOG_ERROR("Error parsing line: {}", line);
        return tl::unexpected(possibleRenderable.error());
      }
      prev = curr + 1;
    }
  }

  IRIS_LOG_LEAVE();
  return {};
} // ParseSAVG

} // namespace iris::io

std::function<std::system_error(void)>
iris::io::LoadSAVG(std::filesystem::path const& path) noexcept {
  IRIS_LOG_ENTER();

  if (auto&& bytes = ReadFile(path)) {
    if (auto ret = ParseSAVG(*bytes, path); !ret) {
      IRIS_LOG_ERROR("Error parsing SAVG: {}", ret.error().what());
      return []() { return std::system_error(Error::kFileLoadFailed); };
    }
  } else {
    IRIS_LOG_LEAVE();
    IRIS_LOG_ERROR("Error reading {}: {}", path.string(), bytes.error().what());
    return []() { return std::system_error(Error::kFileLoadFailed); };
  }

  IRIS_LOG_LEAVE();
  return []() { return std::system_error(Error::kNone); };
} // iris::io::LoadGLTF
