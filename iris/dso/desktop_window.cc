#include "dso/desktop_window.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "config.h"
#include "dso/dso.h"
#include "error.h"
#include "logging.h"
#include "renderer/renderer.h"
#include "wsi/window.h"

std::error_code iris::DesktopWindow::Initialize() noexcept {
  IRIS_LOG_ENTER();

  if (auto win = wsi::Window::Create("DesktopWindow", {720, 720})) {
    window_ = std::move(*win);
  } else {
    GetLogger()->error("Unable to create DesktopWindow window: {}",
                        win.error().message());
    return win.error();
  }

  window_.OnResize([&](glm::uvec2 const& newExtent) {
    GetLogger()->info("DesktopWindow window resized: ({}x{})", newExtent[0],
                      newExtent[1]);
    sResized_ = true;
  });

  window_.OnClose([]() {
    GetLogger()->info("DesktopWindow window closing");
    Renderer::Terminate();
  });

  if (auto sfc = Renderer::Surface::Create(window_)) {
    surface_ = std::move(*sfc);
  } else {
    GetLogger()->error("Unable to create DesktopWindow surface: {}",
                        sfc.error().message());
    return sfc.error();
  }

  window_.Move({320, 320});
  window_.Show();

  IRIS_LOG_LEAVE();
  return Error::kNone;
} // iris::DesktopWindow::Initialize

std::error_code iris::DesktopWindow::Frame() noexcept {
  window_.PollEvents();

  if (sResized_) {
    surface_.Resize(window_.Extent());
    sResized_ = false;
  }

  return Error::kNone;
} // iris::DesktopWindow::Frame

std::error_code
iris::DesktopWindow::Control(std::string_view,
                             std::vector<std::string_view> const& components) noexcept {
  IRIS_LOG_ENTER();

  if (components.size() < 2) {
    GetLogger()->warn("Empty command; ignoring");
    IRIS_LOG_LEAVE();
    return Error::kNone;
  }

  glm::uvec2 newExtent{0, 0};

  std::size_t const numComponents = components.size();
  for (std::size_t i = 2; i < numComponents; ++i) {

    if (absl::StartsWithIgnoreCase(components[i], "WIDTH")) {
      if (i + 1 >= numComponents) {
        GetLogger()->error("Invalid command; WIDTH with no number");
        IRIS_LOG_LEAVE();
        return Error::kInvalidControlCommand;
      }

      if (!absl::SimpleAtoi(components[++i], &newExtent[0])) {
        GetLogger()->error("Invalid command; WIDTH number bad format");
        IRIS_LOG_LEAVE();
        return Error::kInvalidControlCommand;
      }

    } else if (absl::StartsWithIgnoreCase(components[i], "HEIGHT")) {
      if (i + 1 >= numComponents) {
        GetLogger()->error("Invalid command; HEIGHT with no number");
        IRIS_LOG_LEAVE();
        return Error::kInvalidControlCommand;
      }

      if (!absl::SimpleAtoi(components[++i], &newExtent[1])) {
        GetLogger()->error("Invalid command; HEIGHT number bad format");
        IRIS_LOG_LEAVE();
        return Error::kInvalidControlCommand;
      }
    }

  }

  if (newExtent[0] != 0 || newExtent[1] != 0) {
    if (newExtent[0] == 0) newExtent[0] = window_.Extent()[0];
    if (newExtent[1] == 0) newExtent[1] = window_.Extent()[1];
    window_.Resize(newExtent);
  }

  IRIS_LOG_LEAVE();
  return Error::kNone;
} // iris::DesktopWindow::Control

iris::DesktopWindow::~DesktopWindow() noexcept {
  IRIS_LOG_ENTER();
  IRIS_LOG_LEAVE();
} // iris::DesktopWindow::~DesktopWindow

