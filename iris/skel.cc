#include "absl/debugging/failure_signal_handler.h"
#include "absl/debugging/symbolize.h"
#include "iris/config.h"
#include "iris/renderer/renderer.h"
#include "iris/wsi/window.h"
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

int main(int argc, char** argv) {
  absl::InitializeSymbolizer(argv[0]);
  absl::InstallFailureSignalHandler({});

  flags::args const args(argc, argv);
  auto const& files = args.positional();

  auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("skel.log");
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

  // Simulate "DSO desktopWindow" command from desktopWindow.iris file
  iris::Renderer::Control("DSO desktopWindow");

  for (auto&& file : files) iris::Renderer::LoadFile(file);

  logger.info("exiting");
}
