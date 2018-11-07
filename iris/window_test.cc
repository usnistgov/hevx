#include "absl/debugging/failure_signal_handler.h"
#include "absl/debugging/symbolize.h"
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
#include <cstdlib>

int main(int, char** argv) {
  absl::InitializeSymbolizer(argv[0]);
  absl::InstallFailureSignalHandler({});

  auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
  console_sink->set_level(spdlog::level::trace);

  auto logger = std::make_shared<spdlog::logger>("iris", console_sink);
  logger->set_level(spdlog::level::trace);
  spdlog::register_logger(logger);
  spdlog::set_pattern("[%Y-%m-%d %T.%e] [%t] [%n] %^[%l] %v%$");

  logger->info("initialized");


  auto decorated = iris::wsi::Window::Create(
    "decorated", iris::wsi::Offset2D{0, 0}, iris::wsi::Extent2D{300, 300},
    iris::wsi::Window::Options::kDecorated |
      iris::wsi::Window::Options::kSizeable,
    0);
  if (!decorated) {
    logger->error("Cannot create decorated window: {}",
                  decorated.error().what());
    std::exit(EXIT_FAILURE);
  }

  decorated->OnMove([&logger](iris::wsi::Offset2D const& newOffset) {
    logger->debug("decorated OnMove: ({}, {})", newOffset.x, newOffset.y);
  });

  decorated->OnResize([&logger](iris::wsi::Extent2D const& newExtent) {
    logger->debug("decorated OnResize: ({}, {})", newExtent.width,
                  newExtent.height);
  });

  decorated->OnClose([&logger]() { logger->debug("decorated OnClose"); });


  auto undecorated = iris::wsi::Window::Create(
    "undecorated", iris::wsi::Offset2D{350, 0}, iris::wsi::Extent2D{300, 300},
    iris::wsi::Window::Options::kSizeable, 0);
  if (!undecorated) {
    logger->error("Cannot create undecorated window: {}",
                  undecorated.error().what());
    std::exit(EXIT_FAILURE);
  }

  undecorated->OnMove([&logger](iris::wsi::Offset2D const& newOffset) {
    logger->debug("undecorated OnMove: ({}, {})", newOffset.x, newOffset.y);
  });

  undecorated->OnResize([&logger](iris::wsi::Extent2D const& newExtent) {
    logger->debug("undecorated OnResize: ({}, {})", newExtent.width,
                  newExtent.height);
  });

  undecorated->OnClose([&logger]() { logger->debug("undecorated OnClose"); });


  auto nonresizeable = iris::wsi::Window::Create(
    "nonresizeable", iris::wsi::Offset2D{0, 0}, iris::wsi::Extent2D{300, 300},
    iris::wsi::Window::Options::kDecorated, 0);
  if (!nonresizeable) {
    logger->error("Cannot create nonresizeable window: {}",
                  nonresizeable.error().what());
    std::exit(EXIT_FAILURE);
  }

  nonresizeable->OnMove([&logger](iris::wsi::Offset2D const& newOffset) {
    logger->debug("nonresizeable OnMove: ({}, {})", newOffset.x, newOffset.y);
  });

  nonresizeable->OnResize([&logger](iris::wsi::Extent2D const& newExtent) {
    logger->debug("nonresizeable OnResize: ({}, {})", newExtent.width,
                  newExtent.height);
  });

  nonresizeable->OnClose(
    [&logger]() { logger->debug("nonresizeable OnClose"); });

  bool closed = decorated->IsClosed() && undecorated->IsClosed() &&
                nonresizeable->IsClosed();
  while (!closed) {
    decorated->PollEvents();
    undecorated->PollEvents();
    closed = decorated->IsClosed() && undecorated->IsClosed() &&
             nonresizeable->IsClosed();
  }

  logger->info("exiting");
}

