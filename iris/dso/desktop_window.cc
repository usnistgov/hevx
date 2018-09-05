#include "dso/desktop_window.h"
#include "config.h"
#include "dso/dso.h"
#include "logging.h"
#include "renderer/renderer.h"
#include "wsi/window.h"

std::error_code iris::DesktopWindow::Initialize() noexcept {
  IRIS_LOG_ENTER(sGetLogger());

  if (auto win = wsi::Window::Create("DesktopWindow", {720, 720})) {
    window_ = std::move(*win);
  } else {
    sGetLogger()->error("Unable to create DesktopWindow window: {}",
                        win.error().message());
    return win.error();
  }

  window_.OnResize([&](glm::uvec2 const&) {
    sGetLogger()->info("DesktopWindow window resized");
    sResized_ = true;
  });

  window_.OnClose([]() {
    sGetLogger()->info("DesktopWindow window closing");
    Renderer::Terminate();
  });

  if (auto sfc = Renderer::Surface::Create(window_)) {
    surface_ = std::move(*sfc);
  } else {
    sGetLogger()->error("Unable to create DesktopWindow surface: {}",
                        sfc.error().message());
    return sfc.error();
  }

  window_.Move({320, 320});
  window_.Show();

  IRIS_LOG_LEAVE(sGetLogger());
  return {};
} // iris::DesktopWindow::Initialize

std::error_code iris::DesktopWindow::Frame() noexcept {
  window_.PollEvents();

  if (sResized_) {
    surface_.Resize(window_.Extent());
    sResized_ = false;
  }

  return {};
} // iris::DesktopWindow::Frame

std::error_code
iris::DesktopWindow::Control(std::string_view,
                             std::vector<std::string_view> const&) noexcept {
  IRIS_LOG_ENTER(sGetLogger());
  IRIS_LOG_LEAVE(sGetLogger());
  return {};
} // iris::DesktopWindow::Control

