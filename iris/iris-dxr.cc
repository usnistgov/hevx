/*! \file
 * \brief main rendering application
 */
#include "iris/config.h"
#include "iris/io/json.h"
#include "iris/io/read_file.h"
#include "iris/string_util.h"
#include "iris/renderer.h"
#include "iris/renderer_private.h"
#include "iris/shader.h"

#include "absl/container/flat_hash_set.h"
#include "absl/debugging/failure_signal_handler.h"
#include "absl/debugging/symbolize.h"
#include "fmt/format.h"
#include "nlohmann/json.hpp"
#include "spdlog/logger.h"
#include "spdlog/sinks/ansicolor_sink.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include <algorithm>
#include <cstdlib>
#include <exception>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

static char const* sCubeVertexShaderSource [[maybe_unused]] = R"(#version 460 core
layout(push_constant) uniform PushConstants {
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

layout(set = 0, binding = 0) uniform MatricesBuffer {
  mat4 ViewMatrix;
  mat4 ViewMatrixInverse;
  mat4 ProjectionMatrix;
  mat4 ProjectionMatrixInverse;
};

layout(location = 0) out vec4 Po; // surface position in object-space
layout(location = 1) out vec4 Pe; // surface position in eye-space

out gl_PerVertex {
  vec4 gl_Position;
};

void main() {
  // https://www.gamedev.net/forums/topic/674733-vertex-to-cube-using-geometry-shader/
  // topology: tristrip
  // num vertices: 14
  int b = 1 << gl_VertexIndex;
  float x = (0x287a & b) != 0;
  float y = (0x02af & b) != 0;
  float z = (0x31e3 & b) != 0;

  Po = vec4(x, y, z, 1.f);
  Pe = ModelViewMatrix * Po;
  gl_Position = ProjectionMatrix * Pe;
})";

static char const* sCubeFragmentShaderSource [[maybe_unused]] = R"(#version 460 core
#define MAX_LIGHTS 100

struct Light {
  vec4 direction;
  vec4 color;
};

layout(push_constant) uniform PushConstants {
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

layout(set = 0, binding = 0) uniform MatricesBuffer {
  mat4 ViewMatrix;
  mat4 ViewMatrixInverse;
  mat4 ProjectionMatrix;
  mat4 ProjectionMatrixInverse;
};

layout(set = 0, binding = 1) uniform LightsBuffer {
  Light Lights[MAX_LIGHTS];
  int NumLights;
};

layout(location = 0) in vec4 Po; // surface position in object-space
layout(location = 1) in vec4 Pe; // surface position in eye-space

layout(location = 0) out vec4 Color;

void main() {
  vec3 C = vec3(0.8, 0.2, 0.2);
  Color = vec4(pow(C.rgb, vec3(1.0/2.2)), C.a);
})";

using nlohmann::json;

bool compare(json const& encoding, json const& a, json const& b) {
  auto&& field = encoding["field"].get<std::string>();
  auto&& type = encoding["type"].get<std::string>();
  if (type == "quantitative") {
    return a[field].get<double>() < b[field].get<double>();
  } else if (type == "nominal") {
    return a[field].get<std::string>() < b[field].get<std::string>();
  } else if (type == "ordinal") {
    return a[field].get<long long>() < b[field].get<long long>();
  } else {
    std::terminate();
  }
} // compare

template <typename T>
std::pair<T, T> minmax_element(json const& encoding, json const& data) {
  auto&& p = std::minmax_element(
    data.begin(), data.end(),
    [&](json const& a, json const& b) { return compare(encoding, a, b); });
  auto&& field = encoding["field"].get<std::string>();
  return std::make_pair(T{}, T{});
  //return {(*p.first)[field].get<T>(), (*p.second)[field].get<T>()};
} // minmax_element

#if PLATFORM_WINDOWS
#include <Windows.h>
#include <shellapi.h>

int WINAPI WinMain(_In_ HINSTANCE, _In_opt_ HINSTANCE, _In_ LPSTR, _In_ int) {
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

int main(int , char** argv) {

#endif

  absl::InitializeSymbolizer(argv[0]);
  absl::InstallFailureSignalHandler({});

  auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
    "iris-dxr.log", true);
  file_sink->set_level(spdlog::level::trace);

  auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
  console_sink->set_level(spdlog::level::trace);

  spdlog::logger logger("iris-dxr", {console_sink, file_sink});
  logger.set_level(spdlog::level::trace);

  logger.info("Logging initialized");

  if (auto result = iris::Renderer::Initialize(
        "iris-dxr",
        iris::Renderer::Options::kReportDebugMessages |
          iris::Renderer::Options::kEnableValidation,
        {console_sink, file_sink}, 0);
      !result) {
    logger.critical("cannot initialize renderer: {}", result.error().what());
    std::exit(EXIT_FAILURE);
  }

  json cars_spec_data{{"url", "assets/data/cars.json"}};
  json cars_spec_encoding_x{{"field", "Horsepower"}, {"type", "quantitative"}};
  json cars_spec_encoding_y{{"field", "Miles_per_Gallon"},
                            {"type", "quantitative"}};
  json cars_spec_encoding_z{{"field", "Displacement"},
                            {"type", "quantitative"}};
  json cars_spec_encoding_color{{"field", "Origin"}, {"type", "nominal"}};
  json cars_spec_encoding{{"x", cars_spec_encoding_x},
                          {"y", cars_spec_encoding_y},
                          {"z", cars_spec_encoding_z},
                          {"color", cars_spec_encoding_color}};

  json cars_spec{{"data", cars_spec_data},
                 {"mark", "cube"},
                 {"encoding", cars_spec_encoding}};

  logger.debug("spec: {}", cars_spec.dump(2));

  json data;
  if (auto&& bytes =
        iris::io::ReadFile(cars_spec["data"]["url"].get<std::string>())) {
    data = json::parse(*bytes);
  } else {
    logger.error("Error reading {}: {}",
                 cars_spec["data"]["url"].get<std::string>(),
                 bytes.error().what());
  }

  if (!data.is_array()) { logger.error("Error: data is not an array"); }
  logger.info("initial data size: {}", data.size());

  // remove all values in data where the field value is null
  data.erase(
    std::remove_if(
      data.begin(), data.end(),
      [encoding = cars_spec["encoding"]](json const& v) {
        return v[encoding["x"]["field"].get<std::string>()].is_null() ||
               v[encoding["y"]["field"].get<std::string>()].is_null() ||
               v[encoding["z"]["field"].get<std::string>()].is_null() ||
               v[encoding["color"]["field"].get<std::string>()].is_null();
      }),
    data.end());
  logger.info("cleaned data size: {}", data.size());

  auto&& x_range = minmax_element<double>(cars_spec["encoding"]["x"], data);
  logger.info("x range: {} {}", x_range.first, x_range.second);

  auto&& y_range = minmax_element<double>(cars_spec["encoding"]["y"], data);
  logger.info("y range: {} {}", y_range.first, y_range.second);

  auto&& z_range = minmax_element<double>(cars_spec["encoding"]["z"], data);
  logger.info("z range: {} {}", z_range.first, z_range.second);

  auto&& color_field =
    cars_spec["encoding"]["color"]["field"].get<std::string>();

  absl::flat_hash_set<std::string> color_keys;
  for (auto&& elem : data) {
    color_keys.insert(elem[color_field].get<std::string>());
  }

  logger.info("color keys:");
  for (auto&& key : color_keys) logger.info("    {}", key);

  if (auto result = iris::Renderer::LoadFile("configs/desktop.json");
        !result) {
    logger.error("Error loading configs/desktop.json: {}",
                 result.error().what());
  }

  while (iris::Renderer::IsRunning()) {
    iris::Renderer::BeginFrame();

    iris::Renderer::EndFrame();
  }

  logger.info("exiting");
}
