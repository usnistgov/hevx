#ifndef HEV_IRIS_WSI_WINDOW_WIN32_H_
#define HEV_IRIS_WSI_WINDOW_WIN32_H_
/*! \file
 * \brief \ref iris::wsi::Window::Impl declaration for Win32.
 */

#include "wsi/window.h"
#include <Windows.h>

namespace iris::wsi {

//! \brief Platform-defined window handle.
struct Window::NativeHandle_t {
  ::HINSTANCE hInstance{0}; //<! The Win32 instance handle
  ::HWND hWnd{0};           //<! The Win32 window handle
};

/*! \brief Platform-specific window for Win32.
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
  Create(gsl::czstring<> title, Rect rect, Options const& options,
         int) noexcept;

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
    POINT pos;
    ::GetCursorPos(&pos);
    return {pos.x, pos.y};
  }

  /*! \brief Change the title of this window.
   * \param[in] title the new title.
   */
  void Retitle(gsl::czstring<> title) noexcept {
    ::SetWindowText(handle_.hWnd, title);
  }

  /*! \brief Move this window.
   * \param[in] offset the new window offset in screen coordinates.
   */
  void Move(glm::uvec2 const& offset) {
    ::SetWindowPos(handle_.hWnd, HWND_NOTOPMOST, offset[0], offset[1], 0, 0,
                   SWP_NOSIZE);
  }

  /*! \brief Resize this window.
   * \param[in] extent the new window extent in screen coordinate.s
   */
  void Resize(glm::uvec2 const& extent) {
    RECT rect;
    ::SetRect(&rect, 0, 0, extent[0], extent[1]);
    ::AdjustWindowRect(&rect, dwStyle_, FALSE);
    ::SetWindowPos(handle_.hWnd, HWND_NOTOPMOST, 0, 0, (rect.right - rect.left),
                   (rect.bottom - rect.top), SWP_NOMOVE | SWP_NOREPOSITION);
  }

  /*! \brief Indicates if this window has been closed.
   * \return true if this window has been closed, false if not.
   */
  bool IsClosed() const noexcept { return closed_; }

  //! \brief Close this window.
  void Close() noexcept { ::SendMessageA(handle_.hWnd, WM_CLOSE, 0, 0); }

  //! \brief Show this window.
  void Show() noexcept { ::ShowWindow(handle_.hWnd, SW_SHOW); }

  //! \brief Hide this window.
  void Hide() noexcept { ::ShowWindow(handle_.hWnd, SW_HIDE); }

  //! \brief Poll for all outstanding window events. Must be regularly called.
  void PollEvents() noexcept {
    MSG msg = {};
    msg.message = WM_NULL;
    while (::PeekMessage(&msg, 0, 0, 0, PM_REMOVE)) {
      ::TranslateMessage(&msg);
      ::DispatchMessage(&msg);
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
  ~Impl() noexcept {}

private:
  Rect rect_{};
  NativeHandle_t handle_{};
  DWORD dwStyle_{};
  bool closed_{false};
  wsi::Keys keyLUT_[Keyset::kMaxKeys]{};
  Keyset keys_{};
  Buttonset buttons_{};
  CloseDelegate closeDelegate_{[]() {}};
  MoveDelegate moveDelegate_{[](auto) {}};
  ResizeDelegate resizeDelegate_{[](auto) {}};

  ::LRESULT CALLBACK Dispatch(::UINT uMsg, ::WPARAM wParam,
                              ::LPARAM lParam) noexcept;

  static ::WNDCLASSA sWindowClass;
  static ::LRESULT CALLBACK WndProc(::HWND hWnd, ::UINT uMsg, ::WPARAM wParam,
                                    ::LPARAM lParam) noexcept;
}; // class Window::Impl

} // namespace iris::wsi

#endif // HEV_IRIS_WSI_WINDOW_WIN32_H_

