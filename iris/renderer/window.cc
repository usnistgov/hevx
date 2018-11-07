#include "renderer/window.h"
#include "renderer/impl.h"
#include "absl/strings/str_split.h"
#include "error.h"
#include "logging.h"
#include "renderer/renderer.h"

tl::expected<iris::Renderer::Window, std::exception>
iris::Renderer::Window::Create(gsl::czstring<> title, wsi::Offset2D offset,
                               wsi::Extent2D extent,
                               glm::vec4 const& clearColor,
                               Options const& options, int display) noexcept {
  IRIS_LOG_ENTER();

  wsi::Window::Options windowOptions = wsi::Window::Options::kSizeable;
  if ((options & Window::Options::kDecorated) == Window::Options::kDecorated) {
    windowOptions |= wsi::Window::Options::kDecorated;
  }

  Window window;
  if (auto win = wsi::Window::Create(
        title, std::move(offset), std::move(extent), windowOptions, display)) {
    window.window = std::move(*win);
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(win.error());
  }

  if (auto sfc = Surface::Create(window.window, clearColor)) {
    window.surface = std::move(*sfc);
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(sfc.error());
  }

  window.window.Show();

  window.window.OnResize(
    std::bind(&Window::Resize, &window, std::placeholders::_1));
  window.window.OnClose(std::bind(&Window::Close, &window));

  IRIS_LOG_LEAVE();
  return std::move(window);
} // iris::Renderer::Window::Create

void iris::Renderer::Window::Resize(wsi::Extent2D const& newExtent) noexcept {
  GetLogger()->debug("Window resized: ({}x{})", newExtent.width,
                     newExtent.height);
  resized = true;
} // iris::Renderer::Window::Resize

void iris::Renderer::Window::Close() noexcept {
  GetLogger()->debug("Window closing");
  Renderer::Terminate();
} // iris::Renderer::Window::Close

std::error_code iris::Renderer::Window::BeginFrame() noexcept {
  window.PollEvents();

  if (resized) {
    auto const extent = window.Extent();
    surface.Resize({extent.width, extent.height});
    resized = false;
  }

  return Error::kNone;
} // iris::Renderer::Window::BeginFrame

void iris::Renderer::Window::EndFrame() noexcept {
} // iris::Renderer::Window::EndFrame

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

