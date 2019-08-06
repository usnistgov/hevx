/*!
\file
\brief \ref iris::wsi::PlatformWindow::Impl declaration for Win32.
*/
#ifndef HEV_IRIS_WSI_PLATFORM_WINDOW_WIN32_H_
#define HEV_IRIS_WSI_PLATFORM_WINDOW_WIN32_H_

#include "iris/types.h"

#include "absl/container/fixed_array.h"
#include "gsl/gsl"
#include "wsi/platform_window.h"
#include <Windows.h>
#include <cstdint>
#include <cstring>
#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <tuple>

namespace iris::wsi {

/*!
\brief Platform-defined window handle.
*/
struct PlatformWindow::NativeHandle_t {
  ::HINSTANCE hInstance{0};
  ::HWND hWnd{0};

  operator std::tuple<::HINSTANCE&, ::HWND&>() {
    return std::tie(hInstance, hWnd);
  }
};

/*!
\brief Platform-specific window for Win32.

Most of the methods are defined class-inline to hopefully get the compiler to
optimize away the function call at the call-site in \ref PlatformWindow.
*/
class PlatformWindow::Impl {
public:
  /*!
  \brief Create a new Impl.
  \param[in] title the window title.
  \param[in] offset the window offset in screen coordinates.
  \param[in] extent the window extent in screen coordinates.
  \param[in] options the Options describing how to create the window.
  \return a std::expected of either the Impl pointer or a std::exception.
  */
  static expected<std::unique_ptr<Impl>, std::exception>
  Create(gsl::czstring<> title, Offset2D offset, Extent2D extent,
         Options const& options, int);

  /*!
  \brief Get the current rect of this window in screen coordinates.
  \return the Rect2D current rect of this window in screen coordinates.
  */
  Rect2D Rect() const noexcept { return rect_; }

  /*!
  \brief Get the current offset of this window in screen coordinates.
  \return the Offset2D current offset of this window in screen coordinates.
  */
  Offset2D Offset() const noexcept { return rect_.offset; }

  /*!
  \brief Get the current extent of this window in screen coordinates.
  \return the Extent2D current extent of this window in screen coordinates.
  */
  Extent2D Extent() const noexcept { return rect_.extent; }

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
  void Retitle(gsl::czstring<> title) noexcept {
    ::SetWindowText(handle_.hWnd, title);
  }

  /*!
  \brief Move this window.
  \param[in] offset the new window offset in screen coordinates.
  */
  void Move(Offset2D const& offset) {
    ::SetWindowPos(handle_.hWnd, HWND_NOTOPMOST, offset.x, offset.y, 0, 0,
                   SWP_NOSIZE);
  }

  /*!
  \brief Resize this window.
  \param[in] extent the new window extent in screen coordinate.s
  */
  void Resize(Extent2D const& extent) {
    RECT rect;
    ::SetRect(&rect, 0, 0, extent.width, extent.height);
    ::AdjustWindowRect(&rect, dwStyle_, FALSE);
    ::SetWindowPos(handle_.hWnd, HWND_NOTOPMOST, 0, 0, (rect.right - rect.left),
                   (rect.bottom - rect.top), SWP_NOMOVE | SWP_NOREPOSITION);
  }

  /*!
  \brief Indicates if this window has been closed.
  \return true if this window has been closed, false if not.
  */
  bool IsClosed() const noexcept { return closed_; }

  /*!
  \brief Close this window.
  */
  void Close() noexcept {
    closed_ = true;
    closeDelegate_();
    ::DestroyWindow(handle_.hWnd);
  }

  /*!
  \brief Show this window.
  */
  void Show() noexcept { ::ShowWindow(handle_.hWnd, SW_SHOW); }

  /*!
  \brief Hide this window.
  */
  void Hide() noexcept { ::ShowWindow(handle_.hWnd, SW_HIDE); }

  /*!
  \brief Indicates if this window currently has the WSI focus.
   \return true if this window is the focused window, false if not.
  */
  bool IsFocused() const noexcept { return focused_; }

  /*!
  \brief Poll for all outstanding window events. Must be regularly called.
  */
  void PollEvents() noexcept {
    MSG msg = {};
    msg.message = WM_NULL;
    while (::PeekMessage(&msg, 0, 0, 0, PM_REMOVE)) {
      ::TranslateMessage(&msg);
      ::DispatchMessage(&msg);
    }
  }

  /*!
  \brief Set the delegate to be called on window close.
  \param[in] delegate the \ref CloseDelegate.
  */
  void OnClose(CloseDelegate delegate) noexcept { closeDelegate_ = delegate; }

  /*!
  \brief Set the delegate to be called on window move.
  \param[in] delegate the \ref MoveDelegate.
  */
  void OnMove(MoveDelegate delegate) noexcept { moveDelegate_ = delegate; }

  /*!
  \brief Set the delegate to be called on window resize.
  \param[in] delegate the \ref ResizeDelegate.
  */
  void OnResize(ResizeDelegate delegate) noexcept {
    resizeDelegate_ = delegate;
  }

  /*!
  \brief Get the platform-defined window handle.
  \return \ref NativeHandle_t
  */
  NativeHandle_t NativeHandle() const noexcept { return handle_; }

  /*!
  \brief Default constructor: no initialization.
  */
  Impl() noexcept
    : keyLUT_(256) {}

  Impl(Impl const&) = default;
  Impl(Impl&&) = default;
  Impl& operator=(Impl const&) = default;
  Impl& operator=(Impl&&) = default;

  /*!
  \brief Destructor.
  */
  ~Impl() noexcept;

private:
  Rect2D rect_{};
  NativeHandle_t handle_{};
  DWORD dwStyle_{};
  bool closed_{false};
  bool focused_{false};
  absl::FixedArray<int> keyLUT_;
  CloseDelegate closeDelegate_{[]() {}};
  MoveDelegate moveDelegate_{[](auto) {}};
  ResizeDelegate resizeDelegate_{[](auto) {}};

  ::LRESULT CALLBACK Dispatch(::UINT uMsg, ::WPARAM wParam,
                              ::LPARAM lParam) noexcept;

  static ::WNDCLASSA sWindowClass;
  static ::LRESULT CALLBACK WndProc(::HWND hWnd, ::UINT uMsg, ::WPARAM wParam,
                                    ::LPARAM lParam) noexcept;
}; // class PlatformWindow::Impl

} // namespace iris::wsi

#endif // HEV_IRIS_WSI_PLATFORM_WINDOW_WIN32_H_
