#ifndef HEV_IRIS_RENDERER_WINDOW_H_
#define HEV_IRIS_RENDERER_WINDOW_H_

#include "iris/renderer/surface.h"
#include "tl/expected.hpp"
#include <memory>
#include <system_error>

namespace iris::Renderer {

struct Window {
  static tl::expected<Window, std::error_code>
  Create(gsl::czstring<> title, glm::uvec2 extent) noexcept;

  bool resized{false};
  wsi::Window window{};
  Surface surface{};

  void Resize(glm::uvec2 const& newExtent) noexcept;
  void Close() noexcept;

  std::error_code Frame() noexcept;

  Window() = default;
  Window(Window const&) = delete;
  Window(Window&& other) noexcept;
  Window& operator=(Window const&) = delete;
  Window& operator=(Window&& other) noexcept;
  ~Window() noexcept = default;
}; // struct Window

} // namespace iris::Renderer

#endif // HEV_IRIS_RENDERER_WINDOW_H_

