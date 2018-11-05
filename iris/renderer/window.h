#ifndef HEV_IRIS_RENDERER_WINDOW_H_
#define HEV_IRIS_RENDERER_WINDOW_H_

#include "iris/renderer/surface.h"
#include "tl/expected.hpp"
#include "glm/vec4.hpp"
#include <memory>
#include <system_error>

namespace iris::Renderer {

struct Window {
  //! \brief Options for window creation.
  enum class Options {
    kNone = (0),           //!< The window has no options.
    kDecorated = (1 << 0), //!< The window has decorations (title bar, borders).
    kSizeable = (1 << 1),  //!< The window is sizeable.
    kStereo = (1 << 2),    //!< The window will have stereo output.
    kShowUI = (1 << 3),    //!< The window will have UI shown on it.
  };

  // forward-declare this so that it can be used below in Create
  friend Options operator|(Options const& lhs, Options const& rhs) noexcept;

  static tl::expected<Window, std::error_code>
  Create(gsl::czstring<> title, glm::uvec2 offset, glm::uvec2 extent,
         glm::vec4 const& clearColor, Options const& options,
         int display) noexcept;

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

//! \brief bit-wise or of \ref Window::Options.
inline Window::Options operator|(Window::Options const& lhs,
                                 Window::Options const& rhs) noexcept {
  using U = std::underlying_type_t<Window::Options>;
  return static_cast<Window::Options>(static_cast<U>(lhs) |
                                      static_cast<U>(rhs));
}

//! \brief bit-wise or of \ref Window::Options.
inline Window::Options operator|=(Window::Options& lhs,
                                  Window::Options const& rhs) noexcept {
  lhs = lhs | rhs;
  return lhs;
}

//! \brief bit-wise and of \ref Window::Options.
inline Window::Options operator&(Window::Options const& lhs,
                                 Window::Options const& rhs) noexcept {
  using U = std::underlying_type_t<Window::Options>;
  return static_cast<Window::Options>(static_cast<U>(lhs) &
                                      static_cast<U>(rhs));
}

//! \brief bit-wise and of \ref Window::Options.
inline Window::Options operator&=(Window::Options& lhs,
                                  Window::Options const& rhs) noexcept {
  lhs = lhs & rhs;
  return lhs;
}

} // namespace iris::Renderer

#endif // HEV_IRIS_RENDERER_WINDOW_H_

