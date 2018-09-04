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

namespace iris {

static spdlog::logger* sGetLogger() noexcept {
  static std::shared_ptr<spdlog::logger> sLogger = spdlog::get("iris");
  return sLogger.get();
}

} // namespace iris::Renderer

iris::DesktopWindow::DesktopWindow() {
  IRIS_LOG_ENTER(sGetLogger());
  IRIS_LOG_LEAVE(sGetLogger());
}

iris::DesktopWindow::~DesktopWindow() = default;

std::error_code
iris::DesktopWindow::Control(std::string_view,
                             std::vector<std::string_view> const&) noexcept {
  IRIS_LOG_ENTER(sGetLogger());
  IRIS_LOG_LEAVE(sGetLogger());
  return {};
}
