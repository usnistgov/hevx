#ifndef HEV_IRIS_RENDERER_WINDOW_H_
#define HEV_IRIS_RENDERER_WINDOW_H_

#include "iris/renderer/surface.h"
#include "iris/logging.h"
#include <system_error>

namespace iris::Renderer {

struct Window {
  bool resized{false};
  wsi::Window window{};
  Surface surface{};

  std::error_code Frame() noexcept {
    window.PollEvents();
    if (resized) {
      surface.Resize(window.Extent());
      resized = false;
    }
    return Error::kNone;
  }

  template <class W, class S>
  Window(W w, S s)
    : window(std::forward<W>(w))
    , surface(std::forward<S>(s)) {
    IRIS_LOG_ENTER();

    window.OnResize([&](glm::uvec2 const& newExtent) {
      GetLogger()->info("Window resized: ({}x{})", newExtent[0], newExtent[1]);
      resized = true;
    });

    window.OnClose([]() {
      GetLogger()->info("Window closing");
      Renderer::Terminate();
    });

    window.Move({320, 320});
    window.Show();
    IRIS_LOG_LEAVE();
  }

  Window() = default;
  Window(Window const&) = delete;
  Window& operator=(Window const&) = delete;
  ~Window() noexcept = default;

  Window(Window&& other) noexcept
    : resized(other.resized)
    , window(std::move(other.window))
    , surface(std::move(other.surface)) {}

  Window& operator=(Window&& other) noexcept {
    if (this == &other) return *this;
    resized = other.resized;
    window = std::move(other.window);
    surface = std::move(other.surface);
    return *this;
  }
}; // struct Window

} // namespace iris::Renderer

#endif // HEV_IRIS_RENDERER_WINDOW_H_

