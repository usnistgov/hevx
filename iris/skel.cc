#include "absl/debugging/failure_signal_handler.h"
#include "absl/debugging/symbolize.h"
#include "iris/config.h"
#include "iris/renderer/renderer.h"
#include "iris/renderer/io.h"
#include "iris/wsi/window.h"
#include "fmt/format.h"
#if PLATFORM_COMPILER_MSVC
#pragma warning(push)
#pragma warning(disable: 4127)
#endif
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/spdlog.h"
#if PLATFORM_COMPILER_MSVC
#pragma warning(pop)
#endif
#include "flags.h"

#if PLATFORM_WINDOWS
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
      argv[argc++] = p;
    }
  }

#else

int main(int argc, char** argv) {

#endif

  fmt::print("argc: {} &argc: {}", argc, static_cast<void*>(&argc));

  absl::InitializeSymbolizer(argv[0]);
  absl::InstallFailureSignalHandler({});

  flags::args const args(argc, argv);
  auto const& files = args.positional();

  auto file_sink =
    std::make_shared<spdlog::sinks::basic_file_sink_mt>("skel.log", true);
  file_sink->set_level(spdlog::level::trace);

  auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
  console_sink->set_level(spdlog::level::trace);

  spdlog::logger logger("skel", {console_sink, file_sink});
  logger.set_level(spdlog::level::trace);

  logger.info("initialized");

  if (auto error =
        iris::Renderer::Initialize("skel", 0, {console_sink, file_sink})) {
    logger.critical("unable to initialize renderer: {}", error.message());
    std::exit(EXIT_FAILURE);
  }

  for (auto&& file : files) iris::Renderer::io::LoadFile(std::string(file));

  while (iris::Renderer::IsRunning()) { iris::Renderer::Frame(); }

  iris::Renderer::Shutdown();
  logger.info("exiting");
}

