#include "dso/desktop_window.h"
#include "config.h"
#include "dso/dso.h"
#include "wsi/window.h"
#if PLATFORM_COMPILER_MSVC
#pragma warning(push)
#pragma warning(disable : 4127)
#endif
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

std::error_code iris::DesktopWindow::DesktopWindow::Initialize() noexcept {
  IRIS_LOG_ENTER(sGetLogger());

  if (auto win = wsi::Window::Create("DesktopWindow", {720, 720}); !win) {
    sGetLogger()->error("Unable to create DesktopWindow window: {}",
                        win.error().message());
    return win.error();
  } else {
    window_ = std::move(*win);
  }

  if (auto sfc = Renderer::Surface::Create(window_); !sfc) {
    sGetLogger()->error("Unable to create DesktopWindow surface: {}",
                        sfc.error().message());
    return sfc.error();
  } else {
    surface_ = std::move(*sfc);
  }

  window_.Move({320, 320});
  window_.Show();

  IRIS_LOG_LEAVE(sGetLogger());
  return {};
}

std::error_code
iris::DesktopWindow::Control(std::string_view,
                             std::vector<std::string_view> const&) noexcept {
  IRIS_LOG_ENTER(sGetLogger());
  IRIS_LOG_LEAVE(sGetLogger());
  return {};
}
