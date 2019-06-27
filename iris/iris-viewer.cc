/*! \file
 * \brief main rendering application
 */
#include "iris/config.h"
#include "iris/renderer.h"

#if PLATFORM_COMPILER_MSVC
#include <codeanalysis/warnings.h>
#pragma warning(push)
#pragma warning(disable: ALL_CODE_ANALYSIS_WARNINGS)
#pragma warning(disable: ALL_CPPCORECHECK_WARNINGS)
#endif

#include "absl/debugging/failure_signal_handler.h"
#include "absl/debugging/symbolize.h"
#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "fmt/format.h"
#include "spdlog/logger.h"
#include "spdlog/sinks/ansicolor_sink.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include <cstdlib>
#include <exception>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

ABSL_FLAG(std::string, shadertoy_url, "", "ShaderToy URL to load");

#if PLATFORM_COMPILER_MSVC
#pragma warning(pop)
#endif

#if PLATFORM_WINDOWS
extern "C" {
_declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
}

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

int main(int argc, char** argv) {

#endif

  absl::InitializeSymbolizer(argv[0]);
  absl::InstallFailureSignalHandler({});

  auto const positional = absl::ParseCommandLine(argc, argv);
  auto const shadertoy_url = absl::GetFlag(FLAGS_shadertoy_url);

  auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
    "iris-viewer.log", true);
  file_sink->set_level(spdlog::level::trace);

  auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
  console_sink->set_level(spdlog::level::trace);

  spdlog::logger logger("iris-viewer", {console_sink, file_sink});
  logger.set_level(spdlog::level::trace);

  logger.info("Logging initialized");

  if (auto result = iris::Renderer::Initialize(
        "iris-viewer",
        iris::Renderer::Options::kReportDebugMessages |
          iris::Renderer::Options::kEnableValidation,
        {console_sink, file_sink}, 0);
      !result) {
    logger.critical("cannot initialize renderer: {}", result.error().what());
    std::exit(EXIT_FAILURE);
  }

  logger.info("Renderer initialized. {} files specified on command line.",
    positional.size() - 1);

  for (size_t i = 1; i < positional.size(); ++i) {
    logger.info("Loading {}", positional[i]);
    if (auto result = iris::Renderer::LoadFile(positional[i]); !result) {
      logger.error("Error loading {}: {}", positional[i],
                   result.error().what());
    }
  }

  if (!shadertoy_url.empty()) {
    iris::io::json node = {
      {"extras", {{"HEV", {{"shadertoy", {{"url", shadertoy_url}}}}}}}};

    iris::io::json scene = {{"nodes", {0}}};

    iris::io::json gltf = {{"asset", {{"version", "2.0"}}},
                           {"scene", 0},
                           {"scenes", {scene}},
                           {"nodes", {node}}};

    if (auto result = iris::Renderer::LoadGLTF(gltf); !result) {
      logger.error("Error loading {}: {}", shadertoy_url,
                   result.error().what());
    }
  }

  while (iris::Renderer::IsRunning()) {
    iris::Renderer::BeginFrame();

    iris::Renderer::EndFrame();
  }

  logger.info("exiting");
}
