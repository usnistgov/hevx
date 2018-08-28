#ifndef HEV_IRIS_WSI_WINDOW_H_
#define HEV_IRIS_WSI_WINDOW_H_
/*! \file
 * \brief \ref iris::wsi::Window declaration.
 */

#include "glm/vec2.hpp"
#include "iris/wsi/input.h"
#include "tl/expected.hpp"
#include <cstdint>
#include <functional>
#include <memory>
#include <system_error>
#include <type_traits>

/*! \brief IRIS Windowing System Interface (WSI)
 *
 * The main public entry points are wsi::Window and wsi::Input. However, these
 * classes are really for consumption inside Renderer.
 */
namespace iris::wsi {

/*! \brief Manages a platform-specific Window.
 *
 * Windows are created with the \ref Create method. The \ref PollEvents method
 * of each created Window must be called on a regular basis (each time through
 * the render loop) to ensure window system events are correctly processed.
 */
class Window {
public:
  //! \brief Options for window creation.
  enum class Options {
    kDecorated = (1 << 0), //!< The window has decorations (title bar, borders).
    kSizeable = (1 << 1),  //!< The window is sizeable.
  };

  // forward-declare this so that it can be used below in Create
  friend Options operator|(Options const& lhs, Options const& rhs) noexcept;

  /*! \brief Create a new Window.
   * \param[in] title the window title.
   * \param[in] extent the window extent in screen coordinates.
   * \param[in] options the Options describing how to create the window.
   * \return a std::expected of either the Window or a std::error_code.
   */
  static tl::expected<Window, std::error_code> Create(
    char const* title, glm::uvec2 extent,
    Options const& options = Options::kDecorated | Options::kSizeable) noexcept;

  /*! \brief Get the current window offset in screen coordinates.
   * \return the current window offset in screen coordinates.
   */
  glm::uvec2 Offset() const noexcept;

  /*! \brief Get the current window extent in screen coordinates.
   *  \return the current window extent in screen coordinates.
   */
  glm::uvec2 Extent() const noexcept;

  /*! \brief Get the current state of the keyboard.
   *  \return the current state of the keyboard.
   */
  Keyset Keys() const noexcept;

  /*! \brief Get the current state of the buttons.
   *  \return the current state of the buttons.
   */
  Buttonset Buttons() const noexcept;

  /*! \brief Get the current cursor position in screen coordinates.
   *  \return the current cursor position in screen coordinates.
   */
  glm::uvec2 CursorPos() const noexcept;

  /*! \brief Change the title of this window.
   * \param[in] title the new title.
   */
  void Retitle(char const* title) noexcept;

  /*! \brief Move this window.
   * \param[in] offset the new window offset in screen coordinates.
   */
  void Move(glm::uvec2 const& offset);

  /*! \brief Resize this window.
   * \param[in] extent the new window extent in screen coordinate.s
   */
  void Resize(glm::uvec2 const& extent);

  /*! \brief Indicates if this window has been closed.
   * \return true if this window has been closed, false if not.
   */
  bool IsClosed() const noexcept;

  //! \brief Close this window.
  void Close() const noexcept;

  //! \brief Show this window.
  void Show() const noexcept;

  //! \brief Hide this window.
  void Hide() const noexcept;

  //! \brief Poll for all outstanding window events. Must be regularly called.
  void PollEvents() noexcept;

  //! \brief Delegate function called when the window is closed.
  using CloseDelegate = std::function<void()>;

  /*! \brief Set the delegate to be called on window close.
   * \param[in] delegate the \ref CloseDelegate.
   */
  void OnClose(CloseDelegate delegate) noexcept;

  /*! \brief Delegate function called when the window is moved.
   * \param[in] newOffset the new window offset in screen coordinates.
   */
  using MoveDelegate = std::function<void(glm::uvec2 const& newOffset)>;

  /*! \brief Set the delegate to be called on window move.
   * \param[in] delegate the \ref MoveDelegate.
   */
  void OnMove(MoveDelegate delegate) noexcept;

  /*! \brief Delegate function called when the window is resized.
   * \param[in] newExtent the new window extent in screen coordinates.
   */
  using ResizeDelegate = std::function<void(glm::uvec2 const& newExtent)>;

  /*! \brief Set the delegate to be called on window resize.
   * \param[in] delegate the \ref ResizeDelegate.
   */
  void OnResize(ResizeDelegate delegate) noexcept;

  struct NativeHandle_t;

  /*! \brief Get the platform-defined window handle.
   * \return \ref NativeHandle_t
   */
  NativeHandle_t NativeHandle() const noexcept;

  //! \brief Default constructor: no initialization.
  Window() noexcept;
  //! \brief No copies.
  Window(Window const&) = delete;
  //! \brief Move constructor.
  Window(Window&&) noexcept;
  //! \brief No copies.
  Window& operator=(Window const&) = delete;
  //! \brief Move assignment operator.
  Window& operator=(Window&&) noexcept;
  //! \brief Destructor.
  ~Window() noexcept;

private:
  class Impl;
  std::unique_ptr<Impl> pImpl_;
}; // class Window

//! \brief bit-wise or of \ref Window::Options.
inline Window::Options operator|(Window::Options const& lhs,
                                 Window::Options const& rhs) noexcept {
  using U = std::underlying_type_t<Window::Options>;
  return static_cast<Window::Options>(static_cast<U>(lhs) |
                                      static_cast<U>(rhs));
}

//! \brief bit-wise and of \ref Window::Options.
inline Window::Options operator&(Window::Options const& lhs,
                                 Window::Options const& rhs) noexcept {
  using U = std::underlying_type_t<Window::Options>;
  return static_cast<Window::Options>(static_cast<U>(lhs) &
                                      static_cast<U>(rhs));
}

} // namespace iris::wsi

#endif // HEV_IRIS_WSI_WINDOW_H_

