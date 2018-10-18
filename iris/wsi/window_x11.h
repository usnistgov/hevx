#ifndef HEV_IRIS_WSI_WINDOW_X11_H_
#define HEV_IRIS_WSI_WINDOW_X11_H_
/*! \file
 * \brief \ref iris::wsi::Window::Impl declaration for X11.
 */

#include "wsi/window.h"
#include "logging.h"
#include <X11/X.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <cstring>

namespace iris::wsi {

//! \brief Platform-defined window handle.
struct Window::NativeHandle_t {
  ::Display* display{nullptr}; //!< The X11 Display connection.
  ::Window window{0};          //!< The X11 Window handle
};

/*! \brief Platform-specific window for X11.
 * \internal
 * Most of the methods are defined class-inline to hopefully get the compiler
 * to optimize away the function call at the call-site in \ref Window.
 */
class Window::Impl {
public:
  /*! \brief Create a new Impl.
   * \param[in] title the window title.
   * \param[in] extent the window extent in screen coordinates.
   * \param[in] options the Options describing how to create the window.
   * \return a std::expected of either the Impl pointer or a std::error_code.
   */
  static tl::expected<std::unique_ptr<Impl>, std::error_code>
  Create(gsl::czstring<> title, Rect rect, Options const& options, int display);

  /*! \brief Get the current window offset in screen coordinates.
   * \return the current window offset in screen coordinates.
   */
  glm::uvec2 Offset() const noexcept { return rect_.offset; }

  /*! \brief Get the current window extent in screen coordinates.
   *  \return the current window extent in screen coordinates.
   */
  glm::uvec2 Extent() const noexcept { return rect_.extent; }

  /*! \brief Get the current state of the keyboard.
   *  \return the current state of the keyboard.
   */
  Keyset Keys() const noexcept { return keys_; }

  /*! \brief Get the current state of the buttons.
   *  \return the current state of the buttons.
   */
  Buttonset Buttons() const noexcept { return buttons_; }

  /*! \brief Get the current cursor position in screen coordinates.
   *  \return the current cursor position in screen coordinates.
   */
  glm::uvec2 CursorPos() const noexcept {
    ::Window root, child;
    glm::ivec2 rootPos, childPos;
    unsigned int mask;
    ::XQueryPointer(handle_.display, handle_.window, &root, &child, &rootPos[0],
                    &rootPos[1], &childPos[0], &childPos[1], &mask);
    return childPos;
  }

  /*! \brief Change the title of this window.
   * \param[in] title the new title.
   */
  void Retitle(gsl::czstring<> title) noexcept {
    auto const len = std::strlen(title);
    ::XmbSetWMProperties(handle_.display, handle_.window, title, title, nullptr,
                         0, nullptr, nullptr, nullptr);
    ::XChangeProperty(handle_.display, handle_.window, atoms_[NET_WM_NAME],
                      XA_STRING, 8, PropModeReplace,
                      reinterpret_cast<unsigned char const*>(title), len);
    ::XChangeProperty(handle_.display, handle_.window, atoms_[NET_WM_ICON_NAME],
                      XA_STRING, 8, PropModeReplace,
                      reinterpret_cast<unsigned char const*>(title), len);
  }

  /*! \brief Move this window.
   * \param[in] offset the new window offset in screen coordinates.
   */
  void Move(glm::uvec2 const& offset) {
    ::XMoveWindow(handle_.display, handle_.window, offset[0], offset[1]);
  }

  /*! \brief Resize this window.
   * \param[in] extent the new window extent in screen coordinate.s
   */
  void Resize(glm::uvec2 const& extent) {
    ::XResizeWindow(handle_.display, handle_.window, extent[0], extent[1]);
  }

  /*! \brief Indicates if this window has been closed.
   * \return true if this window has been closed, false if not.
   */
  bool IsClosed() const noexcept { return closed_; }

  //! \brief Close this window.
  void Close() noexcept {
    ::XEvent ev = {};
    ev.type = ClientMessage;
    ev.xclient.type = ClientMessage;
    ev.xclient.display = handle_.display;
    ev.xclient.window = handle_.window;
    ev.xclient.message_type = atoms_[WM_PROTOCOLS];
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = atoms_[WM_DELETE_WINDOW];

    ::XSendEvent(handle_.display, handle_.window, False, 0, &ev);
    ::XFlush(handle_.display);
  }

  //! \brief Show this window.
  void Show() noexcept {
    ::XMapWindow(handle_.display, handle_.window);
    ::XFlush(handle_.display);
  }

  //! \brief Hide this window.
  void Hide() noexcept {
    ::XUnmapWindow(handle_.display, handle_.window);
    ::XFlush(handle_.display);
  }

  //! \brief Poll for all outstanding window events. Must be regularly called.
  void PollEvents() noexcept {
    ::XEvent event = {};
    while (::XPending(handle_.display) > 0) {
      ::XNextEvent(handle_.display, &event);
      Dispatch(event);
    }
  }

  /*! \brief Set the delegate to be called on window close.
   * \param[in] delegate the \ref CloseDelegate.
   */
  void OnClose(CloseDelegate delegate) noexcept { closeDelegate_ = delegate; }

  /*! \brief Set the delegate to be called on window move.
   * \param[in] delegate the \ref MoveDelegate.
   */
  void OnMove(MoveDelegate delegate) noexcept { moveDelegate_ = delegate; }

  /*! \brief Set the delegate to be called on window resize.
   * \param[in] delegate the \ref ResizeDelegate.
   */
  void OnResize(ResizeDelegate delegate) noexcept {
    resizeDelegate_ = delegate;
  }

  /*! \brief Get the platform-defined window handle.
   * \return \ref NativeHandle_t
   */
  NativeHandle_t NativeHandle() const noexcept { return handle_; }

  //! \brief Default constructor: no initialization.
  Impl() = default;

  //! \brief Destructor.
  ~Impl() noexcept {
    IRIS_LOG_ENTER();
    ::XCloseDisplay(handle_.display);
    IRIS_LOG_LEAVE();
  }

private:
  enum Atoms {
    WM_PROTOCOLS,
    WM_DELETE_WINDOW,
    NET_WM_NAME,
    NET_WM_ICON_NAME,
    _MOTIF_WM_HINTS,
    kNumAtoms
  }; // enum Atoms

  Rect rect_{};
  NativeHandle_t handle_{};
  ::Visual* visual_{nullptr};
  bool closed_{false};
  wsi::Keys keyLUT_[Keyset::kMaxKeys]{};
  Keyset keys_{};
  Buttonset buttons_{};
  CloseDelegate closeDelegate_{[]() {}};
  MoveDelegate moveDelegate_{[](auto) {}};
  ResizeDelegate resizeDelegate_{[](auto) {}};
  ::Atom atoms_[kNumAtoms]{};

  void Dispatch(::XEvent const& event) noexcept;
  static gsl::czstring<> AtomToString(::Atom atom) noexcept;
}; // class Window::Impl

} // namespace iris::wsi

#endif // HEV_IRIS_WSI_WINDOW_X11_H_

