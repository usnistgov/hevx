#include "renderer/window.h"
#include "absl/strings/str_split.h"
#include "error.h"
#include "logging.h"
#include "renderer/renderer.h"

tl::expected<iris::Renderer::Window, std::error_code>
iris::Renderer::Window::Create(gsl::czstring<> title,
                               glm::uvec2 extent) noexcept {
  IRIS_LOG_ENTER();

  Window window;
  if (auto win = wsi::Window::Create(title, std::move(extent))) {
    window.window = std::move(*win);
  } else {
    GetLogger()->error("Unable to create Window window: {}",
                       win.error().message());
    IRIS_LOG_LEAVE();
    return tl::unexpected(win.error());
  }

  if (auto sfc = Surface::Create(window.window)) {
    window.surface = std::move(*sfc);
  } else {
    GetLogger()->error("Unable to create Window surface: {}",
                       sfc.error().message());
    IRIS_LOG_LEAVE();
    return tl::unexpected(sfc.error());
  }

  window.window.OnResize(
    std::bind(&Window::Resize, &window, std::placeholders::_1));
  window.window.OnClose(std::bind(&Window::Close, &window));

  IRIS_LOG_LEAVE();
  return std::move(window);
} // iris::Renderer::Window::Create

void iris::Renderer::Window::Resize(glm::uvec2 const& newExtent) noexcept {
  GetLogger()->debug("Window resized: ({}x{})", newExtent[0], newExtent[1]);
  resized = true;
} // iris::Renderer::Window::Resize

void iris::Renderer::Window::Close() noexcept {
  GetLogger()->debug("Window closing");
  Renderer::Terminate();
} // iris::Renderer::Window::Close

std::error_code iris::Renderer::Window::Frame() noexcept {
  window.PollEvents();
  if (resized) {
    surface.Resize(window.Extent());
    resized = false;
  }
  return Error::kNone;
} // iris::Renderer::Window::Frame

iris::Renderer::Window::Window(Window&& other) noexcept
  : resized(other.resized)
  , window(std::move(other.window))
  , surface(std::move(other.surface)) {
  // Re-bind delegates
  window.OnResize(std::bind(&Window::Resize, this, std::placeholders::_1));
  window.OnClose(std::bind(&Window::Close, this));
} // iris::Renderer::Window::Window

iris::Renderer::Window& iris::Renderer::Window::
operator=(Window&& other) noexcept {
  if (this == &other) return *this;

  resized = other.resized;
  window = std::move(other.window);
  surface = std::move(other.surface);

  // Re-bind delegates
  window.OnResize(std::bind(&Window::Resize, this, std::placeholders::_1));
  window.OnClose(std::bind(&Window::Close, this));

  return *this;
} // iris::Renderer::Window::operator=
