#include "renderer/window.h"
#include "absl/strings/str_split.h"
#include "error.h"
#include "logging.h"
#include "renderer/renderer.h"

tl::expected<iris::Renderer::Window, std::error_code>
iris::Renderer::Window::Create(gsl::czstring<> title, glm::uvec2 offset,
                               glm::uvec2 extent, glm::vec4 const& clearColor,
                               Options const& options, int display) noexcept {
  IRIS_LOG_ENTER();

  wsi::Window::Options windowOptions = wsi::Window::Options::kSizeable;
  if ((options & Window::Options::kDecorated) == Window::Options::kDecorated) {
    windowOptions |= wsi::Window::Options::kDecorated;
  }

  Window window;
  if (auto win =
        wsi::Window::Create(title, {std::move(offset), std::move(extent)},
                            windowOptions, display)) {
    window.window = std::move(*win);
  } else {
    GetLogger()->error("Cannot create Window window: {}",
                       win.error().message());
    IRIS_LOG_LEAVE();
    return tl::unexpected(win.error());
  }

  if ((options & Window::Options::kStereo) == Window::Options::kStereo) {
    if (auto ctx = GLContext::Create(window.window)) {
      window.context = std::move(*ctx);
    } else {
      GetLogger()->error("Cannot create OpenGL Context");
      return tl::unexpected(ctx.error());
    }

    window.context.MakeCurrent();

    if (flextInit() != GL_TRUE) {
      GetLogger()->error("Cannot initialize OpenGL extensions");
      IRIS_LOG_LEAVE();
      return tl::unexpected(Error::kInitializationFailed);
    }

    GLboolean stereo;
    glGetBooleanv(GL_STEREO, &stereo);
    bool const hasStereo = (stereo == GL_TRUE) && FLEXT_NV_draw_vulkan_image;
  } else {
    if (auto sfc = Surface::Create(window.window, clearColor)) {
      window.surface = std::move(*sfc);
    } else {
      GetLogger()->error("Cannot create Window surface: {}",
                         sfc.error().message());
      IRIS_LOG_LEAVE();
      return tl::unexpected(sfc.error());
    }
  }

  window.window.Show();

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
  , context(std::move(other.context))
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
  context = std::move(other.context);
  surface = std::move(other.surface);

  // Re-bind delegates
  window.OnResize(std::bind(&Window::Resize, this, std::placeholders::_1));
  window.OnClose(std::bind(&Window::Close, this));

  return *this;
} // iris::Renderer::Window::operator=
