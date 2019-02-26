/*! \file
 * \brief main rendering application
 */
#include "iris/config.h"

/*
Good URLs:

https://www.shadertoy.com/view/MsfGRr
https://www.shadertoy.com/view/XdBGzd
https://www.shadertoy.com/view/lsX3W4
https://www.shadertoy.com/view/ldf3DN
https://www.shadertoy.com/view/4ds3zn
https://www.shadertoy.com/view/lsfGDB
https://www.shadertoy.com/view/llXGR4
https://www.shadertoy.com/view/XsjXR1
https://www.shadertoy.com/view/MdB3Dw
https://www.shadertoy.com/view/4df3Rn
https://www.shadertoy.com/view/XttSz2
https://www.shadertoy.com/view/MsSSWV
https://www.shadertoy.com/view/XslXW2 (outputs sound - how do I do that?)
https://www.shadertoy.com/view/MslGD8
https://www.shadertoy.com/view/lsKcDD
https://www.shadertoy.com/view/Mss3R8
https://www.shadertoy.com/view/4sXXRN
https://www.shadertoy.com/view/4ssSRl
https://www.shadertoy.com/view/4dl3Wl
https://www.shadertoy.com/view/4d2XWV
https://www.shadertoy.com/view/XsXGzn
https://www.shadertoy.com/view/MdB3Rc
https://www.shadertoy.com/view/4lGSDw

*/

#include "absl/debugging/failure_signal_handler.h"
#include "absl/debugging/symbolize.h"
#include "absl/strings/str_split.h"
#if PLATFORM_COMPILER_GCC
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wnon-virtual-dtor" // wow!
#endif
#include "cpprest/http_client.h"
#include "fmt/format.h"
#include "glm/vec3.hpp"
#include "glm/vec4.hpp"
#include "iris/renderer.h"
#include "iris/string_util.h"
#include "iris/wsi/input.h"
#if PLATFORM_COMPILER_MSVC
#pragma warning(push)
#pragma warning(disable : 4127)
#endif
#include "spdlog/logger.h"
#include "spdlog/sinks/ansicolor_sink.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#if PLATFORM_COMPILER_MSVC
#pragma warning(pop)
#endif
#include "flags.h"
#include <cstdlib>
#include <exception>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

using namespace std::string_literals;

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

struct PushConstants {
    glm::vec4 iMouse;
    float iTime;
    float iTimeDelta;
    float iFrameRate;
    float iFrame;
    glm::vec3 iResolution;
    float padding0;
}; // struct PushConstants

tl::expected<iris::Renderer::Component::Renderable, std::system_error>
CreateRenderable(spdlog::logger& logger [[maybe_unused]],
                 std::string_view code) {
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
                           0, sizeof(PushConstants)};

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

#if PLATFORM_WINDOWS
extern "C" {
_declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
}

#include <Windows.h>
#include <shellapi.h>

int CALLBACK WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
  // Oh my goodness
  char* cmdLine = ::GetCommandLineA();
  int argc = 1;
  char* argv[128]; // 128 command line argument max
  argv[0] = cmdLine;

  for (char* p = cmdLine; *p; ++p) {
    if (*p == ' ') {
      *p++ = '\0';
      if (*(p + 1)) argv[argc++] = p;
    }
  }

#else

int main(int argc, char** argv) {

#endif

  absl::InitializeSymbolizer(argv[0]);
  absl::InstallFailureSignalHandler({});

  flags::args const args(argc, argv);
  auto const shader = args.get<std::string>("shader");
  auto const url = args.get<std::string>("url");
  auto const playlist = args.get<std::string>("playlist");
  auto&& files = args.positional();

  auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
    "iris-shadertoy.log", true);
  file_sink->set_level(spdlog::level::trace);

  auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
  console_sink->set_level(spdlog::level::trace);

  spdlog::logger logger("iris-shadertoy", {console_sink, file_sink});
  logger.set_level(spdlog::level::trace);

  logger.info("Logging initialized");

  if (auto result = iris::Renderer::Initialize(
        "iris-shadertoy",
        iris::Renderer::Options::kReportDebugMessages |
          iris::Renderer::Options::kUseValidationLayers,
        0, {console_sink, file_sink});
      !result) {
    logger.critical("cannot initialize renderer: {}", result.error().what());
    std::exit(EXIT_FAILURE);
  }

  logger.info("Renderer initialized.");

  for (auto&& file : files) {
    logger.info("Loading {}", file);
    if (auto result = iris::Renderer::LoadFile(file); !result) {
      logger.error("Error loading {}: {}", file, result.error().what());
    }
  }

  if (url && shader) {
    logger.warn("Both a file and a url were specified, using the file");
  }

  auto getCode = [&](web::http::uri const& uri) {
    std::string code;

    web::http::client::http_client client(uri.to_string());
    client.request(web::http::methods::GET)
      .then(
        [&](web::http::http_response response) -> pplx::task<web::json::value> {
          logger.debug("response status_code: {}", response.status_code());
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
        code = iris::wstring_to_string(renderpass.at(U("code")).as_string());
#else
        code = renderpass.at(U("code")).as_string();
#endif
      })
      .wait();

      return code;
  };

  int currentRenderableIndex = 0;
  std::vector<iris::Renderer::Component::Renderable> renderables;

  if (playlist) {
    std::vector<std::string> const splits = absl::StrSplit(*playlist, ',');

    for (auto&& split : splits) {
      web::http::uri_builder uri;
      uri.set_scheme(U("https"));
      uri.set_host(U("www.shadertoy.com"));
      uri.set_path(U("api/v1/shaders"));
#if PLATFORM_WINDOWS
      uri.append_path(iris::string_to_wstring(split));
#else
      uri.append_path(split);
#endif
      uri.append_query(U("key=BtHKWW"));

      std::string const code = getCode(uri.to_uri());
      if (auto r = CreateRenderable(logger, code)) {
        renderables.push_back(std::move(*r));
        iris::Renderer::AddRenderable(renderables[0]);
      } else {
        logger.error("Error creating renderable: {}", r.error().what());
      }
    }

  } else if (url) {
#if PLATFORM_WINDOWS
    web::http::uri const viewURI(iris::string_to_wstring(*url));
#else
    web::http::uri const viewURI(*url);
#endif

    // grab the last component of the uri path: that's the shaderID
    auto const path = viewURI.path();
    auto const id = path.find_last_of('/');

    if (id == std::string::npos) {
      logger.error("Bad URL: {}", *url);
      std::exit(EXIT_FAILURE);
    }

    web::http::uri_builder apiURI;
    apiURI.set_scheme(viewURI.scheme());
    apiURI.set_host(viewURI.host());
    apiURI.set_path(U("api/v1/shaders"));
    apiURI.append_path(path.substr(id));
    apiURI.append_query(U("key=BtHKWW"));
#if PLATFORM_WINDOWS
    logger.debug("api URI: {}", iris::wstring_to_string(apiURI.to_string()));
#else
    logger.debug("api URI: {}", apiURI.to_string());
#endif

    std::string const code = getCode(apiURI.to_uri());
    if (auto r = CreateRenderable(logger, code)) {
      renderables.push_back(std::move(*r));
      iris::Renderer::AddRenderable(renderables[0]);
    } else {
      logger.error("Error creating renderable: {}", r.error().what());
    }
  } else if (shader) {
    std::ifstream ifs(*shader, std::ios_base::binary | std::ios_base::ate);
    if (!ifs) {
      logger.error("Error loading {}: file not found", *shader);
      std::exit(EXIT_FAILURE);
    }

    std::vector<char> bytes(ifs.tellg());
    ifs.seekg(0, ifs.beg);
    ifs.read(bytes.data(), bytes.size());

    if (auto r = CreateRenderable(logger, bytes.data())) {
      renderables.push_back(std::move(*r));
      iris::Renderer::AddRenderable(renderables[0]);
    } else {
      logger.error("Error creating renderable: {}", r.error().what());
    }
  }

  int currentCommandBufferIndex = 0;

  auto commandBuffers = iris::Renderer::AllocateCommandBuffers(
    VK_COMMAND_BUFFER_LEVEL_SECONDARY, 2);
  if (!commandBuffers) {
    logger.error("Error allocating command buffers: {}",
                 commandBuffers.error().what());
    std::abort();
  }

  VkCommandBufferInheritanceInfo commandBufferII = {};
  commandBufferII.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
  commandBufferII.subpass = 0;
  commandBufferII.framebuffer = VK_NULL_HANDLE;

  VkCommandBufferBeginInfo commandBufferBI = {};
  commandBufferBI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  commandBufferBI.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT |
                          VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
  commandBufferBI.pInheritanceInfo = &commandBufferII;

  PushConstants pushConstants;
  pushConstants.iMouse = {0.f, 0.f, 0.f, 0.f};
  pushConstants.iTimeDelta = 0;
  pushConstants.iTime = 0;
  pushConstants.iFrame = 0;

  while (iris::Renderer::IsRunning()) {
    VkRenderPass renderPass = iris::Renderer::BeginFrame();
    commandBufferII.renderPass = renderPass;

    if (pushConstants.iFrame > 0) {
      if (ImGui::IsMouseDown(iris::wsi::Buttons::kButtonLeft)) {
        pushConstants.iMouse.x = ImGui::GetCursorPosX();
        pushConstants.iMouse.y = ImGui::GetCursorPosY();
        logger.debug("Left down: {} {}", pushConstants.iMouse.x,
                     pushConstants.iMouse.y);
      } else if (ImGui::IsMouseReleased(iris::wsi::Buttons::kButtonLeft)) {
        pushConstants.iMouse.z = ImGui::GetCursorPosX();
        pushConstants.iMouse.w = ImGui::GetCursorPosY();
        logger.debug("Left released: {} {}", pushConstants.iMouse.z,
                     pushConstants.iMouse.w);
      }

      pushConstants.iTimeDelta = ImGui::GetIO().DeltaTime;
      pushConstants.iTime += pushConstants.iTimeDelta;
      pushConstants.iFrameRate = pushConstants.iFrame / pushConstants.iTime;
      pushConstants.iResolution.x = ImGui::GetIO().DisplaySize.x;
      pushConstants.iResolution.y = ImGui::GetIO().DisplaySize.y;
      pushConstants.iResolution.z =
        pushConstants.iResolution.x / pushConstants.iResolution.y;
    }

    VkCommandBuffer cb = (*commandBuffers)[currentCommandBufferIndex];
    vkBeginCommandBuffer(cb, &commandBufferBI);

    vkCmdPushConstants(cb, renderables[currentRenderableIndex].pipelineLayout,
                       VK_SHADER_STAGE_VERTEX_BIT |
                         VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(PushConstants), &pushConstants);

    vkEndCommandBuffer(cb);
    iris::Renderer::EndFrame(gsl::make_span(&cb, 1));

    currentCommandBufferIndex =
      (currentCommandBufferIndex + 1) % commandBuffers->size();
    pushConstants.iFrame += 1.f;

    if (static_cast<int>(pushConstants.iFrame) % 1000 == 0) {
      currentRenderableIndex = (currentRenderableIndex + 1) % renderables.size();
      iris::Renderer::AddRenderable(renderables[currentRenderableIndex]);
    }

  }

  logger.info("exiting");
}
