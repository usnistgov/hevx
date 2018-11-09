#ifndef HEV_IRIS_WSI_WINDOW_X11_H_
#define HEV_IRIS_WSI_WINDOW_X11_H_
/*! \file
 * \brief \ref iris::wsi::Window::Impl declaration for X11.
 */

#include "absl/container/fixed_array.h"
#include "wsi/window.h"
#include <xcb/xcb.h>
#include <xcb/xproto.h>
#include <xcb/xcb_icccm.h>
#include <cstring>

namespace iris::wsi {

//! \brief Platform-defined window handle.
struct Window::NativeHandle_t {
  ::xcb_connection_t* connection{nullptr}; //!< The XCB connection.
  ::xcb_window_t window{}; //!< The XCB window.
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
   * \param[in] offset the window offset in screen coordinates.
   * \param[in] extent the window extent in screen coordinates.
   * \param[in] options the Options describing how to create the window.
   * \return a std::expected of either the Impl pointer or a std::exception.
   */
  static tl::expected<std::unique_ptr<Impl>, std::exception>
  Create(gsl::czstring<> title, Offset2D offset, Extent2D extent,
         Options const& options, int display) noexcept;

  Rect2D Rect() const noexcept { return rect_; }
  Offset2D Offset() const noexcept { return rect_.offset; }
  Extent2D Extent() const noexcept { return rect_.extent; }

  Keyset KeyboardState() const noexcept;

  Buttonset ButtonState() const noexcept { return buttons_; }

  /*! \brief Get the current cursor position in screen coordinates.
   *  \return the current cursor position in screen coordinates.
   */
  glm::uvec2 CursorPos() const noexcept;

  glm::vec2 ScrollWheel() const noexcept { return scroll_; }

  std::string Title() noexcept {
    //::XTextProperty prop;
    //::XGetWMName(handle_.display, handle_.window, &prop);
    return "";//std::string(reinterpret_cast<char*>(prop.value), prop.nitems);
  }

  /*! \brief Change the title of this window.
   * \param[in] title the new title.
   */
  void Retitle(gsl::czstring<> title) noexcept {
    auto const len = std::strlen(title);
    ::xcb_change_property(handle_.connection, XCB_PROP_MODE_REPLACE,
                          handle_.window, atoms_[WM_NAME], XCB_ATOM_STRING, 8,
                          len, title);
    ::xcb_change_property(handle_.connection, XCB_PROP_MODE_REPLACE,
                          handle_.window, atoms_[WM_ICON_NAME], XCB_ATOM_STRING,
                          8, len, title);
    ::xcb_flush(handle_.connection);
  }

  /*! \brief Move this window.
   * \param[in] offset the new window offset in screen coordinates.
   */
  void Move(Offset2D const& offset) {
    std::uint32_t values[2] = {static_cast<std::uint32_t>(offset.x),
                               static_cast<std::uint32_t>(offset.y)};
    ::xcb_configure_window(handle_.connection, handle_.window,
                           XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, values);
    ::xcb_flush(handle_.connection);
  }

  /*! \brief Resize this window.
   * \param[in] extent the new window extent in screen coordinate.s
   */
  void Resize(Extent2D const& extent) {
    std::uint32_t values[2] = {extent.width, extent.height};
    ::xcb_configure_window(handle_.connection, handle_.window,
                           XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT,
                           values);
    ::xcb_flush(handle_.connection);
  }

  /*! \brief Indicates if this window has been closed.
   * \return true if this window has been closed, false if not.
   */
  bool IsClosed() const noexcept { return closed_; }

  //! \brief Close this window.
  void Close() noexcept {
    closed_ = true;
    closeDelegate_();
    ::xcb_destroy_window(handle_.connection, handle_.window);
    ::xcb_flush(handle_.connection);
  }

  //! \brief Show this window.
  void Show() noexcept {
    ::xcb_map_window(handle_.connection, handle_.window);
    ::xcb_flush(handle_.connection);
  }

  //! \brief Hide this window.
  void Hide() noexcept {
    ::xcb_unmap_window(handle_.connection, handle_.window);
    ::xcb_flush(handle_.connection);
  }

  /*! \brief Indicates if this window currently has the WSI focus.
   *  \return true if this window is the focused window, false if not.
   */
  bool IsFocused() const noexcept { return focused_; }

  //! \brief Poll for all outstanding window events. Must be regularly called.
  void PollEvents() noexcept {
    ::xcb_generic_event_t* event = nullptr;
    while ((event = ::xcb_poll_for_event(handle_.connection)) != nullptr) {
      Dispatch(gsl::not_null(event));
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
  Impl() noexcept
    : atoms_(kNumAtoms)
    , keyLUT_(Keyset::kMaxKeys) {}

  //! \brief Destructor.
  ~Impl() noexcept;

private:
  enum Atoms {
    WM_NAME,
    WM_ICON_NAME,
    WM_PROTOCOLS,
    WM_DELETE_WINDOW,
    _MOTIF_WM_HINTS,
    kNumAtoms
  }; // enum Atoms

  Rect2D rect_{Offset2D{}, Extent2D{}};
  NativeHandle_t handle_{};
  absl::FixedArray<::xcb_atom_t> atoms_;
  bool closed_{false};
  bool focused_{false};
  absl::FixedArray<::xcb_keycode_t> keyLUT_;
  Buttonset buttons_{};
  glm::vec2 scroll_{};
  CloseDelegate closeDelegate_{[]() {}};
  MoveDelegate moveDelegate_{[](auto) {}};
  ResizeDelegate resizeDelegate_{[](auto) {}};

  void Dispatch(gsl::not_null<::xcb_generic_event_t*> event) noexcept;
}; // class Window::Impl

} // namespace iris::wsi

#endif // HEV_IRIS_WSI_WINDOW_X11_H_

