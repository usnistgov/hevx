/*! \file
 * \brief \ref iris::wsi::Window::Impl definition for Win32.
 */
#include "wsi/window_win32.h"
#include "absl/base/macros.h"
#include "config.h"
#include "logging.h"
#include "wsi/error.h"

namespace iris::wsi {

Keys TranslateKeycode(WPARAM keyCode) {
  switch (keyCode) {
  case VK_BACK: return Keys::kBackspace;
  case VK_TAB: return Keys::kTab;
  case VK_RETURN: return Keys::kEnter;

  case VK_PAUSE: return Keys::kPause;
  case VK_CAPITAL: return Keys::kCapsLock;

  case VK_ESCAPE: return Keys::kEscape;
  case VK_SPACE: return Keys::kSpace;
  case VK_PRIOR: return Keys::kPageUp;
  case VK_NEXT: return Keys::kPageDown;
  case VK_END: return Keys::kEnd;
  case VK_HOME: return Keys::kHome;
  case VK_LEFT: return Keys::kLeft;
  case VK_RIGHT: return Keys::kRight;
  case VK_DOWN: return Keys::kDown;

  case VK_SNAPSHOT: return Keys::kPrintScreen;
  case VK_INSERT: return Keys::kInsert;
  case VK_DELETE: return Keys::kDelete;

  case 0x30: return Keys::k0;
  case 0x31: return Keys::k1;
  case 0x32: return Keys::k2;
  case 0x33: return Keys::k3;
  case 0x34: return Keys::k4;
  case 0x35: return Keys::k5;
  case 0x36: return Keys::k6;
  case 0x37: return Keys::k7;
  case 0x38: return Keys::k8;
  case 0x39: return Keys::k9;
  case 0x41: return Keys::kA;
  case 0x42: return Keys::kB;
  case 0x43: return Keys::kC;
  case 0x44: return Keys::kD;
  case 0x45: return Keys::kE;
  case 0x46: return Keys::kF;
  case 0x47: return Keys::kG;
  case 0x48: return Keys::kH;
  case 0x49: return Keys::kI;
  case 0x4A: return Keys::kJ;
  case 0x4B: return Keys::kK;
  case 0x4C: return Keys::kL;
  case 0x4D: return Keys::kM;
  case 0x4E: return Keys::kN;
  case 0x4F: return Keys::kO;
  case 0x50: return Keys::kP;
  case 0x51: return Keys::kQ;
  case 0x52: return Keys::kR;
  case 0x53: return Keys::kS;
  case 0x54: return Keys::kT;
  case 0x55: return Keys::kU;
  case 0x56: return Keys::kV;
  case 0x57: return Keys::kW;
  case 0x58: return Keys::kX;
  case 0x59: return Keys::kY;
  case 0x5A: return Keys::kZ;

  case VK_LWIN: return Keys::kLeftSuper;
  case VK_RWIN: return Keys::kRightSuper;

  case VK_NUMPAD0: return Keys::kKeypad0;
  case VK_NUMPAD1: return Keys::kKeypad1;
  case VK_NUMPAD2: return Keys::kKeypad2;
  case VK_NUMPAD3: return Keys::kKeypad3;
  case VK_NUMPAD4: return Keys::kKeypad4;
  case VK_NUMPAD5: return Keys::kKeypad5;
  case VK_NUMPAD6: return Keys::kKeypad6;
  case VK_NUMPAD7: return Keys::kKeypad7;
  case VK_NUMPAD8: return Keys::kKeypad8;
  case VK_NUMPAD9: return Keys::kKeypad9;
  case VK_MULTIPLY: return Keys::kKeypadMultiply;
  case VK_ADD: return Keys::kKeypadAdd;
  case VK_SUBTRACT: return Keys::kKeypadSubtract;
  case VK_DECIMAL: return Keys::kKeypadDecimal;
  case VK_DIVIDE: return Keys::kKeypadDivide;

  case VK_F1: return Keys::kF1;
  case VK_F2: return Keys::kF2;
  case VK_F3: return Keys::kF3;
  case VK_F4: return Keys::kF4;
  case VK_F5: return Keys::kF5;
  case VK_F6: return Keys::kF6;
  case VK_F7: return Keys::kF7;
  case VK_F8: return Keys::kF8;
  case VK_F9: return Keys::kF9;
  case VK_F10: return Keys::kF10;
  case VK_F11: return Keys::kF11;
  case VK_F12: return Keys::kF12;
  case VK_F13: return Keys::kF13;
  case VK_F14: return Keys::kF14;
  case VK_F15: return Keys::kF15;
  case VK_F16: return Keys::kF16;
  case VK_F17: return Keys::kF17;
  case VK_F18: return Keys::kF18;
  case VK_F19: return Keys::kF19;
  case VK_F20: return Keys::kF20;
  case VK_F21: return Keys::kF21;
  case VK_F22: return Keys::kF22;
  case VK_F23: return Keys::kF23;
  case VK_F24: return Keys::kF24;

  case VK_NUMLOCK: return Keys::kNumLock;
  case VK_SCROLL: return Keys::kScrollLock;

  case VK_SHIFT: return Keys::kLeftShift;
  case VK_CONTROL: return Keys::kLeftControl;
  case VK_LSHIFT: return Keys::kLeftShift;
  case VK_RSHIFT: return Keys::kRightShift;
  case VK_LCONTROL: return Keys::kLeftControl;
  case VK_RCONTROL: return Keys::kRightControl;
  case VK_LMENU: return Keys::kLeftAlt;
  case VK_RMENU: return Keys::kRightAlt;
  }

  return Keys::kUnknown;
} // TranslateKeycode

} // namespace iris::wsi

tl::expected<std::unique_ptr<iris::wsi::Window::Impl>, std::error_code>
iris::wsi::Window::Impl::Create(gsl::czstring<> title, Rect rect,
                                Options const& options) noexcept {
  IRIS_LOG_ENTER();

  auto pWin = std::make_unique<Impl>();
  if (!pWin) {
    GetLogger()->critical("Unable to allocate memory");
    std::terminate();
  }

  pWin->rect_ = std::move(rect);

  pWin->handle_.hInstance = ::GetModuleHandleA(nullptr);
  if (pWin->handle_.hInstance == 0) {
    char str[1024];
    ::FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, NULL, ::GetLastError(),
                     MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), str,
                     ABSL_ARRAYSIZE(str), NULL);
    GetLogger()->error("Cannot get module handle: {}", str);
    IRIS_LOG_LEAVE();
    return tl::unexpected(Error::kWin32Error);
  }

  sWindowClass.hInstance = pWin->handle_.hInstance;

  if (::RegisterClass(&sWindowClass) == 0) {
    int err = ::GetLastError();
    if (err != ERROR_CLASS_ALREADY_EXISTS) {
      char str[1024];
      ::FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, NULL, ::GetLastError(),
                       MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), str,
                       ABSL_ARRAYSIZE(str), NULL);
      GetLogger()->error("Cannot register window class: {} ({}) ({})", str);
      IRIS_LOG_LEAVE();
      return tl::unexpected(Error::kWin32Error);
    }
  }

  for (int i = 0; i < Keyset::kMaxKeys; ++i) {
    pWin->keyLUT_[i] = TranslateKeycode(i);
  }

  if ((options & Options::kSizeable) != Options::kSizeable) {
    pWin->dwStyle_ = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
  } else {
    pWin->dwStyle_ = WS_OVERLAPPEDWINDOW;
  }

  RECT rect;
  ::SetRect(&rect, pWin->rect_.offset[0], pWin->rect_.offset[1],
            pWin->rect_.extent[0], pWin->rect_.extent[1]);
  ::AdjustWindowRect(&rect, pWin->dwStyle_, FALSE);

  HWND hWnd = ::CreateWindowExA(
    0, sWindowClass.lpszClassName, title, pWin->dwStyle_, CW_USEDEFAULT,
    CW_USEDEFAULT, (rect.right - rect.left), (rect.bottom - rect.top), 0, 0,
    sWindowClass.hInstance, pWin.get());
  if (hWnd == 0 || pWin->handle_.hWnd == 0) {
    char str[1024];
    ::FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, NULL, ::GetLastError(),
                     MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), str,
                     ABSL_ARRAYSIZE(str), NULL);
    GetLogger()->error("Cannot create window: {}", str);
    IRIS_LOG_LEAVE();
    return tl::unexpected(Error::kWin32Error);
  }

  if ((options & Options::kDecorated) != Options::kDecorated) {
    ::SetWindowLongA(pWin->handle_.hWnd, GWL_STYLE, 0);
  }

  IRIS_LOG_LEAVE();
  return std::move(pWin);
} // iris::wsi::Window::Impl::Create

::LRESULT CALLBACK iris::wsi::Window::Impl::Dispatch(::UINT uMsg,
                                                     ::WPARAM wParam,
                                                     ::LPARAM lParam) noexcept {
  ::LRESULT res = 0;

  switch (uMsg) {
  case WM_KEYDOWN: keys_.set(keyLUT_[wParam]); break;
  case WM_KEYUP: keys_.reset(keyLUT_[wParam]); break;

  case WM_LBUTTONDOWN: buttons_.set(wsi::Buttons::k1); break;
  case WM_MBUTTONDOWN: buttons_.set(wsi::Buttons::k2); break;
  case WM_RBUTTONDOWN: buttons_.set(wsi::Buttons::k3); break;
  case WM_XBUTTONDOWN:
    buttons_.set((GET_XBUTTON_WPARAM(wParam) == XBUTTON1) ? wsi::Buttons::k4
                                                          : wsi::Buttons::k5);
    break;

  case WM_LBUTTONUP: buttons_.reset(wsi::Buttons::k1); break;
  case WM_MBUTTONUP: buttons_.reset(wsi::Buttons::k2); break;
  case WM_RBUTTONUP: buttons_.reset(wsi::Buttons::k3); break;
  case WM_XBUTTONUP:
    buttons_.reset((GET_XBUTTON_WPARAM(wParam) == XBUTTON1) ? wsi::Buttons::k4
                                                            : wsi::Buttons::k5);
    break;

  case WM_MOUSEWHEEL:
    // scroll_ += GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
    break;

  case WM_MOVE:
    if (rect_.offset[0] != LOWORD(lParam) || rect_.offset[1] != HIWORD(lParam)) {
      rect_.offset = {LOWORD(lParam), HIWORD(lParam)};
      moveDelegate_(rect_.offset);
    }
    break;

  case WM_SIZE:
    if (rect_.extent[0] != LOWORD(lParam) || rect_.extent[1] != HIWORD(lParam)) {
      rect_.extent = {LOWORD(lParam), HIWORD(lParam)};
      resizeDelegate_(rect_.extent);
    }
    break;

  case WM_CLOSE:
    closed_ = true;
    closeDelegate_();
    ::DestroyWindow(handle_.hWnd);
    break;

  case WM_DESTROY: ::PostQuitMessage(0); break;

  default: res = ::DefWindowProcA(handle_.hWnd, uMsg, wParam, lParam);
  }

  return res;
} // iris::wsi::Display

::WNDCLASSA iris::wsi::Window::Impl::sWindowClass = {
  CS_OWNDC | CS_HREDRAW | CS_VREDRAW,
  &Impl::WndProc,
  0,
  0,
  0,
  0,
  0,
  0,
  nullptr,
  "HevIrisWsiWindowClass"}; // iris::wsi::Window::Impl::sWindowClass

::LRESULT iris::wsi::Window::Impl::WndProc(::HWND hWnd, ::UINT uMsg,
                                           ::WPARAM wParam,
                                           ::LPARAM lParam) noexcept {
  ::LRESULT res = 0;

  if (uMsg == WM_NCCREATE) {
    auto pImpl = reinterpret_cast<Impl*>(
      reinterpret_cast<::LPCREATESTRUCTA>(lParam)->lpCreateParams);
    if (pImpl) {
      pImpl->handle_.hWnd = hWnd;
      ::SetWindowLongPtrA(pImpl->handle_.hWnd, GWLP_USERDATA,
                          reinterpret_cast<::LONG_PTR>(pImpl));
    }
  }

  auto pImpl =
    reinterpret_cast<Impl*>(::GetWindowLongPtrA(hWnd, GWLP_USERDATA));
  if (pImpl) {
    res = pImpl->Dispatch(uMsg, wParam, lParam);
  } else {
    res = ::DefWindowProcA(hWnd, uMsg, wParam, lParam);
  }

  return res;
} // iris::wsi::Window::Impl::WndProc

