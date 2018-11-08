/*! \file
 * \brief \ref iris::wsi::Window::Impl definition for Win32.
 */
#include "wsi/window_win32.h"
#include "absl/base/macros.h"
#include "logging.h"

tl::expected<std::unique_ptr<iris::wsi::Window::Impl>, std::exception>
iris::wsi::Window::Impl::Create(gsl::czstring<> title, Offset2D offset,
                                Extent2D extent, Options const& options, int) {
  IRIS_LOG_ENTER();

  std::unique_ptr<Impl> pWin;

  try {
    pWin = std::make_unique<Impl>();
  } catch (std::bad_alloc const&) {
    GetLogger()->critical("Cannot allocate memory");
    std::terminate();
  } catch (std::exception const& e) {
    GetLogger()->critical("Unhandled exception from std::make_unique<Impl>");
    return tl::unexpected(e);
  }

  pWin->handle_.hInstance = ::GetModuleHandleA(nullptr);
  if (pWin->handle_.hInstance == 0) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(
      std::error_code(::GetLastError(), std::system_category()),
      "Cannot get module handle"));
  }

  sWindowClass.hInstance = pWin->handle_.hInstance;

  if (::RegisterClass(&sWindowClass) == 0) {
    int err = ::GetLastError();
    if (err != ERROR_CLASS_ALREADY_EXISTS) {
      IRIS_LOG_LEAVE();
      return tl::unexpected(std::system_error(
        std::error_code(::GetLastError(), std::system_category()),
        "Cannot register window class"));
    }
  }

  if ((options & Options::kSizeable) != Options::kSizeable) {
    pWin->dwStyle_ = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
  } else {
    pWin->dwStyle_ = WS_OVERLAPPEDWINDOW;
  }

  RECT r;
  ::SetRect(&r, offset.x, offset.y, extent.width, extent.height);
  ::AdjustWindowRect(&r, pWin->dwStyle_, FALSE);

  HWND hWnd = ::CreateWindowExA(0, sWindowClass.lpszClassName, title,
                                pWin->dwStyle_, offset.x, offset.y,
                                (r.right - r.left), (r.bottom - r.top), 0, 0,
                                sWindowClass.hInstance, pWin.get());
  if (hWnd == 0 || pWin->handle_.hWnd == 0) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(
      std::error_code(::GetLastError(), std::system_category()),
      "Cannot create window"));
  }

  if ((options & Options::kDecorated) != Options::kDecorated) {
    ::SetWindowLongA(pWin->handle_.hWnd, GWL_STYLE, 0);
  }

  pWin->Retitle(title);

  pWin->rect_.offset = std::move(offset);
  pWin->rect_.extent = std::move(extent);

  IRIS_LOG_LEAVE();
  return std::move(pWin);
} // iris::wsi::Window::Impl::Create

static int ToKeycode(iris::wsi::Keys key) noexcept {
  switch(key) {
  case iris::wsi::Keys::kSpace: return VK_SPACE;
  case iris::wsi::Keys::kApostrophe: return 0; // FIXME
  case iris::wsi::Keys::kComma: return 0; // FIXME
  case iris::wsi::Keys::kMinus: return 0; // FIXME
  case iris::wsi::Keys::kPeriod: return 0; // FIXME
  case iris::wsi::Keys::kSlash: return 0; // FIXME
  case iris::wsi::Keys::k0: return 0x30;
  case iris::wsi::Keys::k1: return 0x31;
  case iris::wsi::Keys::k2: return 0x32;
  case iris::wsi::Keys::k3: return 0x33;
  case iris::wsi::Keys::k4: return 0x34;
  case iris::wsi::Keys::k5: return 0x35;
  case iris::wsi::Keys::k6: return 0x36;
  case iris::wsi::Keys::k7: return 0x37;
  case iris::wsi::Keys::k8: return 0x38;
  case iris::wsi::Keys::k9: return 0x39;
  case iris::wsi::Keys::kSemicolon: return 0; // FIXME
  case iris::wsi::Keys::kEqual: return 0; // FIXME
  case iris::wsi::Keys::kA: return 0x41;
  case iris::wsi::Keys::kB: return 0x42;
  case iris::wsi::Keys::kC: return 0x43;
  case iris::wsi::Keys::kD: return 0x44;
  case iris::wsi::Keys::kE: return 0x45;
  case iris::wsi::Keys::kF: return 0x46;
  case iris::wsi::Keys::kG: return 0x47;
  case iris::wsi::Keys::kH: return 0x48;
  case iris::wsi::Keys::kI: return 0x49;
  case iris::wsi::Keys::kJ: return 0x4A;
  case iris::wsi::Keys::kK: return 0x4B;
  case iris::wsi::Keys::kL: return 0x4C;
  case iris::wsi::Keys::kM: return 0x4D;
  case iris::wsi::Keys::kN: return 0x4E;
  case iris::wsi::Keys::kO: return 0x4F;
  case iris::wsi::Keys::kP: return 0x50;
  case iris::wsi::Keys::kQ: return 0x51;
  case iris::wsi::Keys::kR: return 0x52;
  case iris::wsi::Keys::kS: return 0x53;
  case iris::wsi::Keys::kT: return 0x54;
  case iris::wsi::Keys::kU: return 0x55;
  case iris::wsi::Keys::kV: return 0x56;
  case iris::wsi::Keys::kW: return 0x57;
  case iris::wsi::Keys::kX: return 0x58;
  case iris::wsi::Keys::kY: return 0x59;
  case iris::wsi::Keys::kZ: return 0x5A;
  case iris::wsi::Keys::kLeftBracket: return 0; // FIXME
  case iris::wsi::Keys::kBackslash: return 0; // FIXME
  case iris::wsi::Keys::kRightBracket: return 0; // FIXME
  case iris::wsi::Keys::kGraveAccent: return 0; // FIXME
  case iris::wsi::Keys::kEscape: return VK_ESCAPE;
  case iris::wsi::Keys::kEnter: return VK_RETURN;
  case iris::wsi::Keys::kTab: return VK_TAB;
  case iris::wsi::Keys::kBackspace: return VK_BACK;
  case iris::wsi::Keys::kInsert: return VK_INSERT;
  case iris::wsi::Keys::kDelete: return VK_DELETE;
  case iris::wsi::Keys::kRight: return VK_RIGHT;
  case iris::wsi::Keys::kLeft: return VK_LEFT;
  case iris::wsi::Keys::kDown: return VK_DOWN;
  case iris::wsi::Keys::kUp: return VK_UP;
  case iris::wsi::Keys::kPageUp: return VK_PRIOR;
  case iris::wsi::Keys::kPageDown: return VK_NEXT;
  case iris::wsi::Keys::kHome: return VK_HOME;
  case iris::wsi::Keys::kEnd: return VK_END;
  case iris::wsi::Keys::kCapsLock: return 0; // FIXME
  case iris::wsi::Keys::kScrollLock: return 0; // FIXME
  case iris::wsi::Keys::kNumLock: return 0; // FIXME
  case iris::wsi::Keys::kPrintScreen: return 0; // FIXME
  case iris::wsi::Keys::kPause: return 0; // FIXME
  case iris::wsi::Keys::kF1: return VK_F1;
  case iris::wsi::Keys::kF2: return VK_F2;
  case iris::wsi::Keys::kF3: return VK_F3;
  case iris::wsi::Keys::kF4: return VK_F4;
  case iris::wsi::Keys::kF5: return VK_F5;
  case iris::wsi::Keys::kF6: return VK_F6;
  case iris::wsi::Keys::kF7: return VK_F7;
  case iris::wsi::Keys::kF8: return VK_F8;
  case iris::wsi::Keys::kF9: return VK_F9;
  case iris::wsi::Keys::kF10: return VK_F10;
  case iris::wsi::Keys::kF11: return VK_F11;
  case iris::wsi::Keys::kF12: return VK_F12;
  case iris::wsi::Keys::kF13: return VK_F13;
  case iris::wsi::Keys::kF14: return VK_F14;
  case iris::wsi::Keys::kF15: return VK_F15;
  case iris::wsi::Keys::kF16: return VK_F16;
  case iris::wsi::Keys::kF17: return VK_F17;
  case iris::wsi::Keys::kF18: return VK_F19;
  case iris::wsi::Keys::kF19: return VK_F19;
  case iris::wsi::Keys::kF20: return VK_F20;
  case iris::wsi::Keys::kF21: return VK_F21;
  case iris::wsi::Keys::kF22: return VK_F22;
  case iris::wsi::Keys::kF23: return VK_F23;
  case iris::wsi::Keys::kF24: return VK_F24;
  case iris::wsi::Keys::kKeypad0: return VK_NUMPAD0;
  case iris::wsi::Keys::kKeypad1: return VK_NUMPAD1;
  case iris::wsi::Keys::kKeypad2: return VK_NUMPAD2;
  case iris::wsi::Keys::kKeypad3: return VK_NUMPAD3;
  case iris::wsi::Keys::kKeypad4: return VK_NUMPAD4;
  case iris::wsi::Keys::kKeypad5: return VK_NUMPAD5;
  case iris::wsi::Keys::kKeypad6: return VK_NUMPAD6;
  case iris::wsi::Keys::kKeypad7: return VK_NUMPAD7;
  case iris::wsi::Keys::kKeypad8: return VK_NUMPAD8;
  case iris::wsi::Keys::kKeypad9: return VK_NUMPAD9;
  case iris::wsi::Keys::kKeypadDecimal: return VK_DECIMAL;
  case iris::wsi::Keys::kKeypadDivide: return VK_DIVIDE;
  case iris::wsi::Keys::kKeypadMultiply: return VK_MULTIPLY;
  case iris::wsi::Keys::kKeypadSubtract: return VK_SUBTRACT;
  case iris::wsi::Keys::kKeypadAdd: return VK_ADD;
  case iris::wsi::Keys::kKeypadEnter: return 0; // FIXME
  case iris::wsi::Keys::kKeypadEqual: return 0; // FIXME
  case iris::wsi::Keys::kLeftShift: return VK_LSHIFT;
  case iris::wsi::Keys::kLeftControl: return VK_LCONTROL;
  case iris::wsi::Keys::kLeftAlt: return VK_LMENU;
  case iris::wsi::Keys::kLeftSuper: return 0; // FIXME
  case iris::wsi::Keys::kRightShift: return VK_RSHIFT;
  case iris::wsi::Keys::kRightControl: return VK_RCONTROL;
  case iris::wsi::Keys::kRightAlt: return VK_RMENU;
  case iris::wsi::Keys::kRightSuper: return 0; // FIXME
  case iris::wsi::Keys::kMenu: return 0; // FIXME
  default: return 0; //FIXME
  }
} // ToKeycode

iris::wsi::Keyset iris::wsi::Window::Impl::KeyboardState() const noexcept {
  Keyset keyboardState;

  BYTE rawState[256];
  if (!::GetKeyboardState(rawState)) {
    GetLogger()->error("Cannot get keyboard state: {}", ::GetLastError());
    return keyboardState;
  }

  for (int i = 0; i < Keyset::kMaxKeys; ++i) {
    keyboardState[static_cast<Keys>(i)] =
      (rawState[ToKeycode(static_cast<Keys>(i))] & 0x80);
  }

  return keyboardState;
} // iris::wsi::Window::Impl::KeyboardState

glm::uvec2 iris::wsi::Window::Impl::CursorPos() const noexcept {
  glm::uvec2 cursorPos{};

  POINT rawPos;
  if (!::GetCursorPos(&rawPos)) {
    GetLogger()->error("Cannot get cursor pos: {}", ::GetLastError());
    return cursorPos;
  }

  ::ScreenToClient(handle_.hWnd, &rawPos);

  cursorPos.x = rawPos.x;
  cursorPos.y = rawPos.y;
  return cursorPos;
} // iris::wsi::Window::Impl::CursorPos

iris::wsi::Window::Impl::~Impl() noexcept {
  IRIS_LOG_ENTER();
  IRIS_LOG_LEAVE();
} // iris::wsi::Window::Impl::~Impl

::LRESULT CALLBACK iris::wsi::Window::Impl::Dispatch(::UINT uMsg,
                                                     ::WPARAM wParam,
                                                     ::LPARAM lParam) noexcept {
  ::LRESULT res = 0;

  switch (uMsg) {
  case WM_ACTIVATE:
    focused_ = (LOWORD(wParam) == WA_ACTIVE);
    break;

  case WM_CHAR:
    break;

  case WM_KEYDOWN: break;
  case WM_KEYUP: break;

  case WM_LBUTTONDOWN: buttons_[Buttons::kLeft] = true; break;
  case WM_RBUTTONDOWN: buttons_[Buttons::kRight] = true; break;
  case WM_MBUTTONDOWN: buttons_[Buttons::kMiddle] = true; break;
  case WM_XBUTTONDOWN:
    switch(GET_XBUTTON_WPARAM(wParam)) {
    case XBUTTON1: buttons_[Buttons::kButton4] = true; break;
    case XBUTTON2: buttons_[Buttons::kButton5] = true; break;
    }
    break;

  case WM_LBUTTONUP: buttons_[Buttons::kLeft] = false; break;
  case WM_RBUTTONUP: buttons_[Buttons::kRight] = false; break;
  case WM_MBUTTONUP: buttons_[Buttons::kMiddle] = false; break;
  case WM_XBUTTONUP:
    switch(GET_XBUTTON_WPARAM(wParam)) {
    case XBUTTON1: buttons_[Buttons::kButton4] = false; break;
    case XBUTTON2: buttons_[Buttons::kButton5] = false; break;
    }
    break;

  case WM_MOUSEWHEEL:
    scroll_.y += static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) /
                 static_cast<float>(WHEEL_DELTA);
    break;

  case WM_MOUSEHWHEEL:
    scroll_.x += static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) /
                 static_cast<float>(WHEEL_DELTA);
    break;

  case WM_MOVE:
    if (rect_.offset.x != LOWORD(lParam) || rect_.offset.y != HIWORD(lParam)) {
      rect_.offset = Offset2D{gsl::narrow_cast<std::int16_t>(LOWORD(lParam)),
                              gsl::narrow_cast<std::int16_t>(HIWORD(lParam))};
      moveDelegate_(rect_.offset);
    }
    break;

  case WM_SIZE:
    if (rect_.extent.width != LOWORD(lParam) ||
        rect_.extent.height != HIWORD(lParam)) {
      rect_.extent = Extent2D{LOWORD(lParam), HIWORD(lParam)};
      resizeDelegate_(rect_.extent);
    }
    break;

  case WM_CLOSE:
    Close();
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

