#ifndef HEV_IRIS_WSI_PLATFORM_WINDOW_H_
#define HEV_IRIS_WSI_PLATFORM_WINDOW_H_
/*! \file
 * \brief \ref iris::wsi::PlatformWindow declaration.
 */

#include "iris/types.h"

#include "glm/vec2.hpp"
#include "gsl/gsl"
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

/*!
\namespace iris::wsi
\brief IRIS Windowing System Interface (WSI)

The main public entry points are wsi::PlatformWindow and wsi::Input. However,
these classes are really for consumption inside Renderer.
*/
namespace iris::wsi {

/*!
\brief A 2D offset (x, y) in screen pixels.
*/
struct Offset2D {
  std::int16_t x{0}; //!< The x offset in screen pixels.
  std::int16_t y{0}; //!< The y offset in screen pixels.

  /*!
  \brief Default constructor.
  */
  constexpr Offset2D() noexcept = default;

  /*!
  \brief Constructor.
  \param[in] x_ the x offset.
  \param[in] y_ the y offset.
  */
  constexpr explicit Offset2D(std::int16_t x_, std::int16_t y_) noexcept
    : x(std::move(x_))
    , y(std::move(y_)) {}
}; // struct Offset2D

/*!
\brief a 2D extent (width, height) in screen pixels.
*/
struct Extent2D {
  std::uint16_t width{0};  //!< The width in screen pixels.
  std::uint16_t height{0}; //!< The height in screen pixels.

  /*!
  \brief Default constructor.
  */
  constexpr Extent2D() noexcept = default;

  /*!
  \brief Constructor.
  \param[in] w the width.
  \param[in] h the height.
  */
  constexpr explicit Extent2D(std::uint16_t w, std::uint16_t h) noexcept
    : width(std::move(w))
    , height(std::move(h)) {}
}; // struct Extent2D

/*!
\brief a 2D rect (offset, extent) in screen pixels.
*/
struct Rect2D {
  Offset2D offset{}; //!< The offset in screen pixels.
  Extent2D extent{}; //!< The extent in screen pixels.

  /*!
  \brief Default constructor.
  */
  constexpr Rect2D() noexcept = default;

  /*!
  \brief Constructor.
  \param[in] o the Offset2D offset.
  \param[in] e the Extent2D extent.
  */
  constexpr explicit Rect2D(Offset2D o, Extent2D e) noexcept
    : offset(std::move(o))
    , extent(std::move(e)) {}
}; // struct Rect2D

/*!
\brief Manages a platform-specific window.

PlatformWindows are created with the \ref Create method. The \ref PollEvents
method of each created PlatformWindow must be called on a regular basis (each
time through the render loop) to ensure window system events are correctly
processed.
*/
class PlatformWindow {
public:
  //! \brief Options for window creation.
  enum class Options {
    kNone = (0),           //!< The window has no options.
    kDecorated = (1 << 0), //!< The window has decorations (title bar, borders).
    kSizeable = (1 << 1),  //!< The window is sizeable.
  };

  // forward-declare this so that it can be used below in Create
  friend Options operator|(Options const& lhs, Options const& rhs) noexcept;

  /*!
  \brief Create a new PlatformWindow.
  \param[in] title the window title.
  \param[in] offset the window offset in screen coordinates.
  \param[in] extent the window extent in screen coordinates.
  \param[in] options the Options describing how to create the window.
  \return a std::expected of either the PlatformWindow or a std::exception.
  */
  static expected<PlatformWindow, std::exception>
  Create(gsl::czstring<> title, Offset2D offset, Extent2D extent,
         Options const& options, int display) noexcept;

  /*!
  \brief Get the current rect of this window in screen coordinates.
  \return the Rect2D current rect of this window in screen coordinates.
  */
  Rect2D Rect() const noexcept;

  /*!
  \brief Get the current offset of this window in screen coordinates.
  \return the OFfset2D current offset of this window in screen coordinates.
  */
  Offset2D Offset() const noexcept;

  /*!
  \brief Get the current extent of this window in screen coordinates.
  \return the Extent2D current extent of this window in screen coordinates.
  */
  Extent2D Extent() const noexcept;

  /*!
  \brief Get the current cursor position in screen coordinates if this window
  is the active window.
  \return the glm::vec2 current cursor position in screen coordinates if this
  window is the active window, or (-FLT_MAX, -FLT_MAX)
  */
  glm::vec2 CursorPos() const noexcept;

  /*!
  \brief Change the title of this window.
  \param[in] title the new title.
  */
  void Retitle(gsl::czstring<> title) noexcept;

  /*!
  \brief Move this window.
  \param[in] offset the new window offset in screen coordinates.
  */
  void Move(Offset2D const& offset);

  /*!
  \brief Resize this window.
  \param[in] extent the new window extent in screen coordinate.s
  */
  void Resize(Extent2D const& extent);

  /*!
  \brief Indicates if this window has been closed.
  \return true if this window has been closed, false if not.
  */
  bool IsClosed() const noexcept;

  /*!
  \brief Close this window.
  */
  void Close() const noexcept;

  /*!
  \brief Show this window.
  */
  void Show() const noexcept;

  /*!
  \brief Hide this window.
  */
  void Hide() const noexcept;

  /*!
  \brief Indicates if this window currently has the WSI focus.
  \return true if this window is the focused window, false if not.
  */
  bool IsFocused() const noexcept;

  /*!
  \brief Poll for all outstanding window events. Must be regularly called.
  */
  void PollEvents() noexcept;

  /*!
  \brief Delegate function called when the window is closed.
  */
  using CloseDelegate = std::function<void()>;

  /*!
  \brief Set the delegate to be called on window close.
  \param[in] delegate the \ref CloseDelegate.
  */
  void OnClose(CloseDelegate delegate) noexcept;

  /*!
  \brief Delegate function called when the window is moved.
  \param[in] newOffset the new window offset in screen coordinates.
  */
  using MoveDelegate = std::function<void(Offset2D const& newOffset)>;

  /*!
  \brief Set the delegate to be called on window move.
  \param[in] delegate the \ref MoveDelegate.
  */
  void OnMove(MoveDelegate delegate) noexcept;

  /*!
  \brief Delegate function called when the window is resized.
  \param[in] newExtent the new window extent in screen coordinates.
  */
  using ResizeDelegate = std::function<void(Extent2D const& newExtent)>;

  /*!
  \brief Set the delegate to be called on window resize.
  \param[in] delegate the \ref ResizeDelegate.
  */
  void OnResize(ResizeDelegate delegate) noexcept;

  struct NativeHandle_t;

  /*!
  \brief Get the platform-defined window handle.
  \return \ref NativeHandle_t
  */
  NativeHandle_t NativeHandle() const noexcept;

  /*!
  \brief Default constructor: no initialization.
  */
  PlatformWindow() noexcept;

  /*!
  \brief No copies.
  */
  PlatformWindow(PlatformWindow const&) = delete;

  /*!
  \brief Move constructor.
  */
  PlatformWindow(PlatformWindow&&) noexcept;

  /*!
  \brief No copies.
  */
  PlatformWindow& operator=(PlatformWindow const&) = delete;

  /*!
  \brief Move assignment operator.
  */
  PlatformWindow& operator=(PlatformWindow&&) noexcept;

  /*!
  \brief Destructor.
  */
  ~PlatformWindow() noexcept;

private:
  class Impl;
  std::unique_ptr<Impl> pImpl_;
}; // class PlatformWindow

/*!
\brief bit-wise or of \ref PlatformWindow::Options.
*/
inline PlatformWindow::Options
operator|(PlatformWindow::Options const& lhs,
          PlatformWindow::Options const& rhs) noexcept {
  using U = std::underlying_type_t<PlatformWindow::Options>;
  return static_cast<PlatformWindow::Options>(static_cast<U>(lhs) |
                                              static_cast<U>(rhs));
}

/*!
\brief bit-wise or of \ref PlatformWindow::Options.
*/
inline PlatformWindow::Options
operator|=(PlatformWindow::Options& lhs,
           PlatformWindow::Options const& rhs) noexcept {
  lhs = lhs | rhs;
  return lhs;
}

/*!
\brief bit-wise and of \ref PlatformWindow::Options.
*/
inline PlatformWindow::Options
operator&(PlatformWindow::Options const& lhs,
          PlatformWindow::Options const& rhs) noexcept {
  using U = std::underlying_type_t<PlatformWindow::Options>;
  return static_cast<PlatformWindow::Options>(static_cast<U>(lhs) &
                                              static_cast<U>(rhs));
}

/*!
\brief bit-wise and of \ref PlatformWindow::Options.
*/
inline PlatformWindow::Options
operator&=(PlatformWindow::Options& lhs,
           PlatformWindow::Options const& rhs) noexcept {
  lhs = lhs & rhs;
  return lhs;
}

} // namespace iris::wsi

#endif // HEV_IRIS_WSI_PLATFORM_WINDOW_H_

