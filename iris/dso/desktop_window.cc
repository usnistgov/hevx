#include "dso/desktop_window.h"
#include "config.h"
#include "dso/dso.h"
#include "wsi/window.h"
#if PLATFORM_COMPILER_MSVC
#pragma warning(push)
#pragma warning(disable : 4127)
#endif
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/spdlog.h"
#if PLATFORM_COMPILER_MSVC
#pragma warning(pop)
#endif

iris::DesktopWindow::DesktopWindow() {}

iris::DesktopWindow::~DesktopWindow() {}

std::error_code
iris::DesktopWindow::Control(std::string_view,
                             std::vector<std::string_view> const&) noexcept {
  return {};
}
