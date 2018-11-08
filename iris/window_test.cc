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

std::shared_ptr<spdlog::logger> sLogger;

void WindowStyleTest() {
  sLogger->info("Window style test");

  auto decorated = iris::wsi::Window::Create(
    "decorated", iris::wsi::Offset2D{0, 0}, iris::wsi::Extent2D{300, 300},
    iris::wsi::Window::Options::kDecorated |
      iris::wsi::Window::Options::kSizeable,
    0);
  if (!decorated) {
    sLogger->error("Cannot create decorated window: {}",
                   decorated.error().what());
    std::exit(EXIT_FAILURE);
  }

  decorated->OnMove([](iris::wsi::Offset2D const& newOffset) {
    sLogger->debug("decorated OnMove: ({}, {})", newOffset.x, newOffset.y);
  });

  decorated->OnResize([](iris::wsi::Extent2D const& newExtent) {
    sLogger->debug("decorated OnResize: ({}, {})", newExtent.width,
                   newExtent.height);
  });

  decorated->OnClose([]() { sLogger->debug("decorated OnClose"); });


  auto undecorated = iris::wsi::Window::Create(
    "undecorated", iris::wsi::Offset2D{350, 0}, iris::wsi::Extent2D{300, 300},
    iris::wsi::Window::Options::kSizeable, 0);
  if (!undecorated) {
    sLogger->error("Cannot create undecorated window: {}",
                   undecorated.error().what());
    std::exit(EXIT_FAILURE);
  }

  undecorated->OnMove([](iris::wsi::Offset2D const& newOffset) {
    sLogger->debug("undecorated OnMove: ({}, {})", newOffset.x, newOffset.y);
  });

  undecorated->OnResize([](iris::wsi::Extent2D const& newExtent) {
    sLogger->debug("undecorated OnResize: ({}, {})", newExtent.width,
                   newExtent.height);
  });

  undecorated->OnClose([]() { sLogger->debug("undecorated OnClose"); });


  auto nonresizeable = iris::wsi::Window::Create(
    "nonresizeable", iris::wsi::Offset2D{700, 0}, iris::wsi::Extent2D{300, 300},
    iris::wsi::Window::Options::kDecorated, 0);
  if (!nonresizeable) {
    sLogger->error("Cannot create nonresizeable window: {}",
                   nonresizeable.error().what());
    std::exit(EXIT_FAILURE);
  }

  nonresizeable->OnMove([](iris::wsi::Offset2D const& newOffset) {
    sLogger->debug("nonresizeable OnMove: ({}, {})", newOffset.x, newOffset.y);
  });

  nonresizeable->OnResize([](iris::wsi::Extent2D const& newExtent) {
    sLogger->debug("nonresizeable OnResize: ({}, {})", newExtent.width,
                   newExtent.height);
  });

  nonresizeable->OnClose([]() { sLogger->debug("nonresizeable OnClose"); });


  decorated->Show();
  undecorated->Show();
  nonresizeable->Show();

  while (!decorated->IsClosed()) {
    decorated->PollEvents();
    undecorated->PollEvents();
    nonresizeable->PollEvents();
  }

  undecorated->Close();
  nonresizeable->Close();
}

void InputTest() {
  sLogger->info("Input test");

  auto win = iris::wsi::Window::Create("InputTest", iris::wsi::Offset2D{0, 0},
                                       iris::wsi::Extent2D{300, 300},
                                       iris::wsi::Window::Options::kDecorated |
                                         iris::wsi::Window::Options::kSizeable,
                                       0);
  if (!win) {
    sLogger->error("Cannot create win: {}", win.error().what());
    std::exit(EXIT_FAILURE);
  }

  win->OnMove([](iris::wsi::Offset2D const& newOffset) {
    sLogger->debug("win OnMove: ({}, {})", newOffset.x, newOffset.y);
  });

  win->OnResize([](iris::wsi::Extent2D const& newExtent) {
    sLogger->debug("win OnResize: ({}, {})", newExtent.width, newExtent.height);
  });

  win->OnClose([]() { sLogger->debug("win OnClose"); });

  win->Show();

  iris::wsi::Keyset prevKeys, currKeys;
  iris::wsi::Buttonset prevButtons, currButtons;

  while (!win->IsClosed()) {
    win->PollEvents();

    currKeys = win->KeyboardState();
    currButtons = win->Buttons();

    if (!currKeys[iris::wsi::Keys::kEscape] &&
        prevKeys[iris::wsi::Keys::kEscape]) {
      win->Close();
    }

    if (!currButtons[iris::wsi::Buttons::kRight] &&
        prevButtons[iris::wsi::Buttons::kRight]) {
      auto const scroll = win->ScrollWheel();
      sLogger->info("ScrollWheel: ({}, {})", scroll.x, scroll.y);
    }

    prevKeys = currKeys;
    prevButtons = currButtons;
  }
}

int main(int, char** argv) {
  absl::InitializeSymbolizer(argv[0]);
  absl::InstallFailureSignalHandler({});

  auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
  console_sink->set_level(spdlog::level::trace);

  sLogger = std::make_shared<spdlog::logger>("iris", console_sink);
  sLogger->set_level(spdlog::level::trace);
  spdlog::register_logger(sLogger);
  spdlog::set_pattern("[%Y-%m-%d %T.%e] [%t] [%n] %^[%l] %v%$");

  sLogger->info("initialized");

  WindowStyleTest();
  InputTest();

  sLogger->info("exiting");
}

