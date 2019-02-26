#include "iris/config.h"

#include "error.h"
#include "io/shadertoy.h"
#include "components/renderable.h"
#include "logging.h"
#if PLATFORM_COMPILER_GCC
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor" // wow!
#endif
#include "cpprest/http_client.h"
#include "glm/vec3.hpp"
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
  std::string code;

  web::http::client::http_client client(uri.to_string());
  client.request(web::http::methods::GET)
    .then(
      [&](web::http::http_response response) -> pplx::task<web::json::value> {
        GetLogger()->debug(
          "LoadShaderToy::LoadTask::GetCode: response status_code: {}",
          response.status_code());
        return response.extract_json();
      })
    .then([&](web::json::value json) {
      auto&& renderpass = json.at(U("Shader")).at(U("renderpass")).at(0);

      if (renderpass.at(U("inputs")).size() > 0) {
        throw std::runtime_error("inputs are not yet implemented");
      } else if (renderpass.at(U("type")).as_string() != U("image")) {
        throw std::runtime_error("non-image outputs are not yet implemented");
      }

#if PLATFORM_WINDOWS
      code = wstring_to_string(renderpass.at(U("code")).as_string());
#else
      code = renderpass.at(U("code")).as_string();
#endif
    })
    .wait();

  return code;
} // GetCode

class LoadTask : public tbb::task {
public:
  LoadTask(std::string url)
    : url_(std::move(url)) {}

  tbb::task* execute() override {
    IRIS_LOG_ENTER();
#if PLATFORM_WINDOWS
    web::http::uri const viewURI(string_to_wstring(url_));
#else
    web::http::uri const viewURI(url_);
#endif

    // grab the last component of the uri path: that's the shaderID
    auto const path = viewURI.path();
    auto const id = path.find_last_of('/');

#if PLATFORM_WINDOWS
    if (id == std::wstring::npos) {
#else
    if (id == std::string::npos) {
#endif
      GetLogger()->error("Bad URL: {}", url_);
      IRIS_LOG_LEAVE();
      return nullptr;
    }

    web::http::uri_builder apiURI;
    apiURI.set_scheme(viewURI.scheme());
    apiURI.set_host(viewURI.host());
    apiURI.set_path(U("api/v1/shaders"));
    apiURI.append_path(path.substr(id));
    apiURI.append_query(U("key=BtHKWW"));
#if PLATFORM_WINDOWS
    GetLogger()->debug("api URI: {}", wstring_to_string(apiURI.to_string()));
#else
    GetLogger()->debug("api URI: {}", apiURI.to_string());
#endif

    std::string const code = GetCode(apiURI.to_uri());

    if (auto r = CreateRenderable(code)) {
        iris::Renderer::AddRenderable(std::move(*r));
    } else {
      GetLogger()->error("Error creating renderable: {}", r.error().what());
    }

    IRIS_LOG_LEAVE();
    return nullptr;
  } // execute

private:
  std::string url_;
};   // class LoadTask

} // namespace iris::io

std::function<std::system_error(void)>
iris::io::LoadShaderToy(std::string const& url) noexcept {
  IRIS_LOG_ENTER();

  try {
    LoadTask* task = new (tbb::task::allocate_root()) LoadTask(url);
    tbb::task::enqueue(*task);
  } catch (std::exception const& e) {
    GetLogger()->warn("Loading shadertoy failed: {}", e.what());
  }

  IRIS_LOG_LEAVE();
  return [](){ return std::system_error(Error::kNone); };
} // iris::io::LoadShaderToy
