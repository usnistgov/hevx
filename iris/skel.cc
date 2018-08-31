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
#include "spdlog/spdlog.h"
#include "spdlog/spdlog.h"
#if PLATFORM_COMPILER_MSVC
#pragma warning(pop)
#endif

int main(int argc[[maybe_unused]], char** argv[[maybe_unused]]) {
  absl::InitializeSymbolizer(argv[0]);
  absl::InstallFailureSignalHandler({});

  auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
  console_sink->set_level(spdlog::level::trace);
  auto iris_logger = std::make_shared<spdlog::logger>("iris", console_sink);
  iris_logger->set_level(spdlog::level::trace);
  spdlog::register_logger(iris_logger);
  auto logger = std::make_shared<spdlog::logger>("skel", console_sink);
  logger->set_level(spdlog::level::trace);

  logger->info("initialized");

  if (auto error = iris::Renderer::Initialize("skel")) {
    logger->error("unable to initialize renderer: {}", error.message());
    std::exit(EXIT_FAILURE);
  }

  iris::wsi::Window window;
  if (auto win = iris::wsi::Window::Create("skel", {800, 800})) {
    window = std::move(*win);
  } else {
    logger->error("unable to create window: {}", win.error().message());
    std::exit(EXIT_FAILURE);
  }

  window.OnClose([&logger]() { logger->debug("window closing"); });

  window.Show();
  while (!window.IsClosed()) { window.PollEvents(); }

  logger->info("exiting");
}

