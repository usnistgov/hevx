#include "iris/config.h"

#include "error.h"
#include "io/shadertoy.h"
#include "components/renderable.h"
#include "logging.h"
#if PLATFORM_COMPILER_GCC
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor" // wow!
#endif
#define _TURN_OFF_PLATFORM_STRING
#include "cpprest/http_client.h"
#include "glm/vec3.hpp"
#include "io/read_file.h"
#include "renderer_util.h"
#include "string_util.h"
#include "tbb/task.h"

namespace iris::io {

static char const* sVertexShaderSource = R"(#version 450
layout(push_constant) uniform uPC {
    vec4 iMouse;
    float iTime;
    float iTimeDelta;
    float iFrameRate;
    float iFrame;
    vec3 iResolution;
    float padding0;
};

layout(location = 0) out vec2 fragCoord;

void main() {
    fragCoord = vec2((gl_VertexIndex << 1) & 2, (gl_VertexIndex & 2));
    gl_Position = vec4(fragCoord * 2.0 - 1.0, 0.f, 1.0);
    // flip to match shadertoy
    fragCoord.y *= -1;
    fragCoord.y += 1;

    // multiple by resolution to match shadertoy
    fragCoord *= iResolution.xy;
})";

static char const* sFragmentShaderHeader = R"(#version 450
#extension GL_GOOGLE_include_directive : require
layout(push_constant) uniform uPC {
    vec4 iMouse;
    float iTime;
    float iTimeDelta;
    float iFrameRate;
    float iFrame;
    vec3 iResolution;
    float padding0;
};

layout(location = 0) in vec2 fragCoord;
layout(location = 0) out vec4 fragColor;
)";

tl::expected<iris::Renderer::Component::Renderable, std::system_error>
CreateRenderable(std::string_view code) {
  iris::Renderer::Component::Renderable renderable;

  auto vs = iris::Renderer::CompileShaderFromSource(
    sVertexShaderSource, VK_SHADER_STAGE_VERTEX_BIT,
    "iris-shadertoy::Renderable::VertexShader");
  if (!vs) return tl::unexpected(vs.error());

  std::ostringstream fragmentShaderSource;
  fragmentShaderSource << sFragmentShaderHeader << code << R"(

void main() {
    mainImage(fragColor, fragCoord);
})";

  auto fs = iris::Renderer::CompileShaderFromSource(
    fragmentShaderSource.str(), VK_SHADER_STAGE_FRAGMENT_BIT,
    "iris-shadertoy::Renderable::FragmentShader");
  if (!fs) return tl::unexpected(fs.error());

  absl::FixedArray<iris::Renderer::Shader> shaders{
    iris::Renderer::Shader{*vs, VK_SHADER_STAGE_VERTEX_BIT},
    iris::Renderer::Shader{*fs, VK_SHADER_STAGE_FRAGMENT_BIT},
  };

  absl::FixedArray<VkPushConstantRange> pushConstantRanges(1);
  pushConstantRanges[0] = {VK_SHADER_STAGE_VERTEX_BIT |
                             VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(Renderer::ShaderToyPushConstants)};

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

  if (auto pl = iris::Renderer::CreateGraphicsPipeline(
        {}, pushConstantRanges, shaders, {}, {}, inputAssemblyStateCI,
        viewportStateCI, rasterizationStateCI, multisampleStateCI,
        depthStencilStateCI, colorBlendAttachmentStates, dynamicStates, 0,
        "iris-shadertoy::Renderable::Pipeline")) {
    std::tie(renderable.pipelineLayout, renderable.pipeline) = *pl;
  } else {
    return tl::unexpected(pl.error());
  }

  renderable.numVertices = 3;
  return renderable;
} // CreateRenderable

// this throws
std::string GetCode(web::http::uri const& uri) {
  IRIS_LOG_ENTER();
  using namespace web;
  std::string code;

  http::client::http_client client(uri.to_string());
  client.request(http::methods::GET)
    .then([&](http::http_response response) -> pplx::task<json::value> {
      GetLogger()->trace(
        "LoadShaderToy::LoadTask::GetCode: response status_code: {}",
        response.status_code());
      return response.extract_json();
    })
    .then([&](json::value json) {
      GetLogger()->trace("parsing code");

      json::value shader;
      try {
        shader = json.at(_XPLATSTR("Shader"));
      } catch (std::exception const& e) {
        GetLogger()->error("Cannot find Shader in code: {}", e.what());
        IRIS_LOG_LEAVE();
        return;
      }

      json::value renderpasses;
      try {
        renderpasses = shader.at(_XPLATSTR("renderpass"));
      } catch (std::exception const& e) {
        GetLogger()->error("Cannot find renderpass in code: {}", e.what());
        IRIS_LOG_LEAVE();
        return;
      }

      if (!renderpasses.is_array()) {
        GetLogger()->error("Renderpasses is not an array");
        IRIS_LOG_LEAVE();
        return;
      }

      json::value renderpass;
      try {
        renderpass = renderpasses.at(0);
      } catch (std::exception const& e) {
        GetLogger()->error("No renderpass in renderpasses array: {}", e.what());
        IRIS_LOG_LEAVE();
        return;
      }

      if (renderpass.has_field(_XPLATSTR("inputs")) &&
          renderpass.at(_XPLATSTR("inputs")).size() > 0) {
        GetLogger()->error("inputs are not yet implemented");
        IRIS_LOG_LEAVE();
        return;
      }

      if (renderpass.has_field(_XPLATSTR("type")) &&
          renderpass.at(_XPLATSTR("type")).is_string() &&
          renderpass.at(_XPLATSTR("type")).as_string() != _XPLATSTR("image")) {
        GetLogger()->error("non-image outputs are not yet implemented");
        IRIS_LOG_LEAVE();
        return;
      }

      try {
        GetLogger()->trace("converting code");
#if PLATFORM_WINDOWS
        code = wstring_to_string(renderpass.at(_XPLATSTR("code")).as_string());
#else
        code = renderpass.at(_XPLATSTR("code")).as_string();
#endif
      } catch (std::exception const& e) {
        GetLogger()->error("Error converting code: {}", e.what());
      }

      GetLogger()->trace("done parsing code");
    })
    .wait();

  IRIS_LOG_LEAVE();
  return code;
} // GetCode

static void LoadFile(web::http::uri const& uri) {
#if PLATFORM_WINDOWS
  filesystem::path const path = wstring_to_string(uri.path());
#else
  filesystem::path const path = uri.path();
#endif

  std::string code;
  if (auto bytes = ReadFile(path)) {
    code = std::string(reinterpret_cast<char*>(bytes->data()), bytes->size());
  } else {
    GetLogger()->error("Error reading file {}: {}", uri.path(),
                       bytes.error().what());
    IRIS_LOG_LEAVE();
    return;
  }

  GetLogger()->trace("creating renderable");
  if (auto r = CreateRenderable(code)) {
    iris::Renderer::AddRenderable(std::move(*r));
  } else {
    GetLogger()->error("Error creating renderable: {}", r.error().what());
  }
} // LoadFile

static void LoadWeb(web::http::uri const& uri) {
  IRIS_LOG_ENTER();

  // grab the last component of the uri path: that's the shaderID
  auto const path = uri.path();
  auto const id = path.find_last_of('/');

#if PLATFORM_WINDOWS
  if (id == std::wstring::npos) {
    GetLogger()->error("Bad URI: {}", wstring_to_string(uri.to_string()));
#else
  if (id == std::string::npos) {
    GetLogger()->error("Bad URI: {}", uri.to_string());
#endif
    IRIS_LOG_LEAVE();
    return;
  }

  web::http::uri_builder apiURI;
  apiURI.set_scheme(uri.scheme());
  apiURI.set_host(uri.host());
  apiURI.set_path(_XPLATSTR("api/v1/shaders"));
  apiURI.append_path(path.substr(id));
  apiURI.append_query(_XPLATSTR("key=BtHKWW"));
#if PLATFORM_WINDOWS
  GetLogger()->debug("api URI: {}", wstring_to_string(apiURI.to_string()));
#else
  GetLogger()->debug("api URI: {}", apiURI.to_string());
#endif

  std::string const code = GetCode(apiURI.to_uri());

  GetLogger()->trace("creating renderable");
  if (auto r = CreateRenderable(code)) {
    iris::Renderer::AddRenderable(std::move(*r));
  } else {
    GetLogger()->error("Error creating renderable: {}", r.error().what());
  }

  IRIS_LOG_LEAVE();
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
    GetLogger()->debug("Loading scheme: {}", wstring_to_string(uri.scheme()));
#else
    GetLogger()->debug("Loading scheme: {}", uri.scheme());
#endif

    if (uri.scheme() == _XPLATSTR("file")) {
      LoadFile(uri);
    } else if (uri.scheme() == _XPLATSTR("http") ||
               uri.scheme() == _XPLATSTR("https")) {
      LoadWeb(uri);
    } else {
#if PLATFORM_WINDOWS
      GetLogger()->error("Unknown scheme: {}", wstring_to_string(uri.scheme()));
#else
      GetLogger()->error("Unknown scheme: {}", uri.scheme());
#endif
    }

    IRIS_LOG_LEAVE();
    return nullptr;
  } // execute

private:
  std::string url_;
}; // class LoadTask

class CreateTask : public tbb::task {
public:
  CreateTask(Control::ShaderToy::Source const& source) {
    for (int i = 0; i < source.source_size(); ++i) {
      code_ += source.source(i);
      code_ += "\n";
    }
  }

  tbb::task* execute() override {
    IRIS_LOG_ENTER();

    GetLogger()->trace("creating renderable");
    if (auto r = CreateRenderable(code_)) {
      iris::Renderer::AddRenderable(std::move(*r));
    } else {
      GetLogger()->error("Error creating renderable: {}", r.error().what());
    }

    IRIS_LOG_LEAVE();
    return nullptr;
  } // execute

private:
  std::string code_;
}; // class CreateTask

} // namespace iris::io

std::function<std::system_error(void)>
iris::io::LoadShaderToy(Control::ShaderToy const& message) noexcept {
  IRIS_LOG_ENTER();

  switch (message.type_case()) {
  case Control::ShaderToy::TypeCase::kUrl:
    try {
      LoadTask* task = new (tbb::task::allocate_root()) LoadTask(message.url());
      tbb::task::enqueue(*task);
    } catch (std::exception const& e) {
      GetLogger()->warn("Loading shadertoy failed: {}", e.what());
    }
    break;
  case Control::ShaderToy::TypeCase::kCode:
    try {
      CreateTask* task =
        new (tbb::task::allocate_root()) CreateTask(message.code());
      tbb::task::enqueue(*task);
    } catch (std::exception const& e) {
      GetLogger()->warn("Loading shadertoy failed: {}", e.what());
    }
    break;
  default:
    GetLogger()->error("Unsupported controlMessage message type {}",
                       message.type_case());
    break;
  }

  IRIS_LOG_LEAVE();
  return [](){ return std::system_error(Error::kNone); };
} // iris::io::LoadShaderToy
