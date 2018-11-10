#include "absl/debugging/failure_signal_handler.h"
#include "absl/debugging/symbolize.h"
#include "fmt/format.h"
#include "imgui.h"
#include "iris/config.h"
#include "iris/renderer/io.h"
#include "iris/renderer/renderer.h"
#include "iris/wsi/window.h"
#if PLATFORM_COMPILER_MSVC
#pragma warning(push)
#pragma warning(disable : 4127)
#endif
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/spdlog.h"
#if PLATFORM_COMPILER_MSVC
#pragma warning(pop)
#endif
#include "flags.h"

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
  auto const& files = args.positional();

  auto file_sink =
    std::make_shared<spdlog::sinks::basic_file_sink_mt>("iris-viewer.log", true);
  file_sink->set_level(spdlog::level::trace);

  auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
  console_sink->set_level(spdlog::level::trace);

  spdlog::logger logger("iris-viewer", {console_sink, file_sink});
  logger.set_level(spdlog::level::trace);

  logger.info("initialized");

  if (auto error = iris::Renderer::Initialize(
        "iris-viewer",
        iris::Renderer::Options::kReportDebugMessages |
          iris::Renderer::Options::kUseValidationLayers,
        0, {console_sink, file_sink}); error.code()) {
    logger.critical("cannot initialize renderer: {}", error.what());
    std::exit(EXIT_FAILURE);
  }

  for (auto&& file : files) iris::Renderer::io::LoadFile(file);

  while (iris::Renderer::IsRunning()) {
    if (!iris::Renderer::BeginFrame()) continue;

    if (!ImGui::GetIO().WantCaptureKeyboard) {
      if (ImGui::IsKeyReleased(iris::wsi::Keys::kEscape)) {
        iris::Renderer::Terminate();
      }
    }

    ImGui::Begin("Status");
    ImGui::Text("Last Frame %.3f ms", ImGui::GetIO().DeltaTime);
    ImGui::Text("Average %.3f ms/frame (%.1f FPS)",
                1000.f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
    ImGui::End();

    iris::Renderer::EndFrame();
  }

  iris::Renderer::Shutdown();
  logger.info("exiting");
}

