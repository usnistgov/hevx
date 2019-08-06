#include "iris/config.h"

#include "absl/container/fixed_array.h"
#include "components/renderable.h"
#include "error.h"
#include "io/shadertoy.h"
#include "logging.h"
#if PLATFORM_COMPILER_GCC
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor" // wow!
#endif
#define _TURN_OFF_PLATFORM_STRING
#include "cpprest/http_client.h"
#include "glm/vec3.hpp"
#include "io/read_file.h"
#include "renderer.h"
#include "renderer_private.h"
#include "string_util.h"
#include "tbb/task.h"

namespace iris::io {

static char const* sVertexShaderSource = R"(#version 460 core
layout(push_constant) uniform uPC {
    vec4 iMouse;
    float iTime;
    float iTimeDelta;
    float iFrameRate;
    float iFrame;
    vec3 iResolution;
    bool bDebugNormals;
    vec4 EyePosition;
    mat4 ModelMatrix;
    mat4 ModelViewMatrix;
    mat3 NormalMatrix;
};

layout(location = 0) out vec2 fragCoord;

void main() {
    fragCoord = vec2((gl_VertexIndex << 1) & 2, (gl_VertexIndex & 2));
    gl_Position = vec4(fragCoord * 2.0 - 1.0, 0.f, 1.0);

    // We created the vertices for normal Vulkan viewports, but IRIS uses a
    // negative viewport to handle OpenGL shaders, so reflip Y here.
    gl_Position.y *= -1;

    // flip to match shadertoy
    fragCoord.y *= -1;
    fragCoord.y += 1;

    // multiple by resolution to match shadertoy
    fragCoord *= iResolution.xy;
})";

static char const* sFragmentShaderHeader = R"(#version 460 core
#extension GL_GOOGLE_include_directive : require
layout(push_constant) uniform uPC {
    vec4 iMouse;
    float iTime;
    float iTimeDelta;
    float iFrameRate;
    float iFrame;
    vec3 iResolution;
    bool bDebugNormals;
    vec4 EyePosition;
    mat4 ModelMatrix;
    mat4 ModelViewMatrix;
    mat3 NormalMatrix;
};

layout(location = 0) in vec2 fragCoord;
layout(location = 0) out vec4 fragColor;
)";

expected<iris::Renderer::Component::Renderable, std::system_error>
CreateRenderable(std::string_view code) {
  IRIS_LOG_ENTER();
  absl::FixedArray<Shader> shaders(2);

  if (auto vs = CompileShaderFromSource(sVertexShaderSource,
                                        VK_SHADER_STAGE_VERTEX_BIT)) {
    shaders[0] = std::move(*vs);
  } else {
    IRIS_LOG_LEAVE();
    return unexpected(vs.error());
  }

  Renderer::NameObject(VK_OBJECT_TYPE_SHADER_MODULE, shaders[0].module,
                       "iris-shadertoy::Renderable::VertexShader");

  std::ostringstream fragmentShaderSource;
  fragmentShaderSource << sFragmentShaderHeader << code << R"(

void main() {
    mainImage(fragColor, fragCoord);
})";

  if (auto fs = CompileShaderFromSource(fragmentShaderSource.str(),
                                        VK_SHADER_STAGE_FRAGMENT_BIT)) {
    shaders[1] = std::move(*fs);
  } else {
    IRIS_LOG_LEAVE();
    return unexpected(fs.error());
  }

  Renderer::NameObject(VK_OBJECT_TYPE_SHADER_MODULE, shaders[1].module,
                       "iris-shadertoy::Renderable::FragmentShader");

  VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI = {};
  inputAssemblyStateCI.sType =
    VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  inputAssemblyStateCI.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  // The viewport and scissor are specified later as dynamic states
  VkPipelineViewportStateCreateInfo viewportStateCI = {};
  viewportStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewportStateCI.viewportCount = 1;
  viewportStateCI.scissorCount = 1;

  VkPipelineRasterizationStateCreateInfo rasterizationStateCI = {};
  rasterizationStateCI.sType =
    VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterizationStateCI.polygonMode = VK_POLYGON_MODE_FILL;
  rasterizationStateCI.cullMode = VK_CULL_MODE_FRONT_BIT;
  rasterizationStateCI.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rasterizationStateCI.lineWidth = 1.f;

  VkPipelineMultisampleStateCreateInfo multisampleStateCI = {};
  multisampleStateCI.sType =
    VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisampleStateCI.rasterizationSamples = VK_SAMPLE_COUNT_4_BIT;
  multisampleStateCI.minSampleShading = 1.f;

  VkPipelineDepthStencilStateCreateInfo depthStencilStateCI = {};
  depthStencilStateCI.sType =
    VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;

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

  Renderer::Component::Material material;

  if (auto pipe = CreateRasterizationPipeline(
        shaders, {}, {}, inputAssemblyStateCI, viewportStateCI,
        rasterizationStateCI, multisampleStateCI, depthStencilStateCI,
        colorBlendAttachmentStates, dynamicStates, 0, {})) {
    material.pipeline = std::move(*pipe);
  } else {
    IRIS_LOG_LEAVE();
    return unexpected(pipe.error());
  }

  auto materialID = Renderer::AddMaterial(material);

  Renderer::Component::Renderable renderable;
  renderable.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  renderable.material = materialID;
  renderable.numVertices = 3;

  IRIS_LOG_LEAVE();
  return renderable;
} // CreateRenderable

// this throws
std::string GetCode(web::http::uri const& uri) {
  IRIS_LOG_ENTER();
  using namespace web;
  std::string code;

  http::client::http_client client(uri.to_string());
  client.request(http::methods::GET)
    .then([&](http::http_response response) -> pplx::task<web::json::value> {
      IRIS_LOG_TRACE(
        "LoadShaderToy::LoadTask::GetCode: response status_code: {}",
        response.status_code());
      return response.extract_json();
    })
    .then([&](web::json::value json) {
      IRIS_LOG_TRACE("parsing code");

      web::json::value shader;
      try {
        shader = json.at(_XPLATSTR("Shader"));
      } catch (std::exception const& e) {
        IRIS_LOG_ERROR("Cannot find Shader in code: {}", e.what());
        IRIS_LOG_LEAVE();
        return;
      }

      web::json::value renderpasses;
      try {
        renderpasses = shader.at(_XPLATSTR("renderpass"));
      } catch (std::exception const& e) {
        IRIS_LOG_ERROR("Cannot find renderpass in code: {}", e.what());
        IRIS_LOG_LEAVE();
        return;
      }

      if (!renderpasses.is_array()) {
        IRIS_LOG_ERROR("Renderpasses is not an array");
        IRIS_LOG_LEAVE();
        return;
      }

      web::json::value renderpass;
      try {
        renderpass = renderpasses.at(0);
      } catch (std::exception const& e) {
        IRIS_LOG_ERROR("No renderpass in renderpasses array: {}", e.what());
        IRIS_LOG_LEAVE();
        return;
      }

      if (renderpass.has_field(_XPLATSTR("inputs")) &&
          renderpass.at(_XPLATSTR("inputs")).size() > 0) {
        IRIS_LOG_ERROR("inputs are not yet implemented");
        IRIS_LOG_LEAVE();
        return;
      }

      if (renderpass.has_field(_XPLATSTR("type")) &&
          renderpass.at(_XPLATSTR("type")).is_string() &&
          renderpass.at(_XPLATSTR("type")).as_string() != _XPLATSTR("image")) {
        IRIS_LOG_ERROR("non-image outputs are not yet implemented");
        IRIS_LOG_LEAVE();
        return;
      }

      try {
        IRIS_LOG_TRACE("converting code");
#if PLATFORM_WINDOWS
        code = wstring_to_string(renderpass.at(_XPLATSTR("code")).as_string());
#else
        code = renderpass.at(_XPLATSTR("code")).as_string();
#endif
      } catch (std::exception const& e) {
        IRIS_LOG_ERROR("Error converting code: {}", e.what());
      }

      IRIS_LOG_TRACE("done parsing code");
    })
    .wait();

  IRIS_LOG_LEAVE();
  return code;
} // GetCode

expected<Renderer::Component::Renderable, std::system_error>
LoadFile(web::http::uri const& uri) {
#if PLATFORM_WINDOWS
  std::filesystem::path const path = wstring_to_string(uri.path());
#else
  std::filesystem::path const path = uri.path();
#endif

  std::string code;
  if (auto bytes = ReadFile(path)) {
    code = std::string(reinterpret_cast<char*>(bytes->data()), bytes->size());
  } else {
    IRIS_LOG_LEAVE();
    return unexpected(bytes.error());
  }

  IRIS_LOG_TRACE("creating renderable");
  if (auto r = CreateRenderable(code)) {
    IRIS_LOG_LEAVE();
    return std::move(*r);
  } else {
    IRIS_LOG_LEAVE();
    return unexpected(r.error());
  }
} // LoadFile

expected<Renderer::Component::Renderable, std::system_error> static LoadWeb(
  web::http::uri const& uri) {
  IRIS_LOG_ENTER();

  // grab the last component of the uri path: that's the shaderID
  auto const path = uri.path();
  auto const id = path.find_last_of('/');

#if PLATFORM_WINDOWS
  if (id == std::wstring::npos) {
    IRIS_LOG_LEAVE();
    return unexpected(std::system_error(Error::kURIInvalid,
                                        wstring_to_string(uri.to_string())));
#else
  if (id == std::string::npos) {
    IRIS_LOG_LEAVE();
    return unexpected(std::system_error(Error::kURIInvalid, uri.to_string()));
#endif
  }

  web::http::uri_builder apiURI;
  apiURI.set_scheme(uri.scheme());
  apiURI.set_host(uri.host());
  apiURI.set_path(_XPLATSTR("api/v1/shaders"));
  apiURI.append_path(path.substr(id));
  apiURI.append_query(_XPLATSTR("key=BtHKWW"));
#if PLATFORM_WINDOWS
  IRIS_LOG_DEBUG("api URI: {}", wstring_to_string(apiURI.to_string()));
#else
  IRIS_LOG_DEBUG("api URI: {}", apiURI.to_string());
#endif

  std::string const code = GetCode(apiURI.to_uri());

  IRIS_LOG_TRACE("creating renderable");
  if (auto r = CreateRenderable(code)) {
    IRIS_LOG_LEAVE();
    return std::move(*r);
  } else {
    IRIS_LOG_LEAVE();
    return unexpected(r.error());
  }
} // LoadWeb

class LoadTask : public tbb::task {
public:
  LoadTask(std::string url)
    : url_(std::move(url)) {}

  tbb::task* execute() override {
    IRIS_LOG_ENTER();
#if PLATFORM_WINDOWS
    web::http::uri const uri(string_to_wstring(url_));
#else
    web::http::uri const uri(url_);
#endif

#if PLATFORM_WINDOWS
    IRIS_LOG_DEBUG("Loading scheme: {}", wstring_to_string(uri.scheme()));
#else
    IRIS_LOG_DEBUG("Loading scheme: {}", uri.scheme());
#endif

    if (uri.scheme() == _XPLATSTR("file")) {
      if (auto r = LoadFile(uri)) {
        Renderer::AddRenderable(std::move(*r));
      } else {
        IRIS_LOG_ERROR("Error loading uri: {}", r.error().what());
      }
    } else if (uri.scheme() == _XPLATSTR("http") ||
               uri.scheme() == _XPLATSTR("https")) {
      if (auto r = LoadWeb(uri)) {
        Renderer::AddRenderable(std::move(*r));
      } else {
        IRIS_LOG_ERROR("Error loading uri: {}", r.error().what());
      }
    } else {
#if PLATFORM_WINDOWS
      IRIS_LOG_ERROR("Unknown scheme: {}", wstring_to_string(uri.scheme()));
#else
      IRIS_LOG_ERROR("Unknown scheme: {}", uri.scheme());
#endif
    }

    IRIS_LOG_LEAVE();
    return nullptr;
  } // execute

private:
  std::string url_;
}; // class LoadTask

} // namespace iris::io

iris::expected<iris::Renderer::Component::Renderable, std::system_error>
iris::io::LoadShaderToy(std::string const& url) {
  IRIS_LOG_ENTER();
#if PLATFORM_WINDOWS
  web::http::uri const uri(string_to_wstring(url));
#else
  web::http::uri const uri(url);
#endif

  if (uri.scheme() == _XPLATSTR("file")) {
    return LoadFile(uri);
  } else if (uri.scheme() == _XPLATSTR("http") ||
             uri.scheme() == _XPLATSTR("https")) {
    return LoadWeb(uri);
  } else {
    return unexpected(std::system_error(Error::kURIInvalid, "unknown scheme"));
  }
} // iris::io::LoadShaderToy
