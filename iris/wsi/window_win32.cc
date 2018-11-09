/*! \file
 * \brief \ref iris::wsi::Window::Impl definition for Win32.
 */
#include "wsi/window_win32.h"
#include "absl/base/macros.h"
#include "logging.h"

namespace iris::wsi {

static int KeysToKeycode(Keys key) noexcept {
  switch(key) {
  case Keys::kSpace: return VK_SPACE;
  case Keys::kApostrophe: return 0; // FIXME
  case Keys::kComma: return 0; // FIXME
  case Keys::kMinus: return 0; // FIXME
  case Keys::kPeriod: return 0; // FIXME
  case Keys::kSlash: return 0; // FIXME
  case Keys::k0: return 0x30;
  case Keys::k1: return 0x31;
  case Keys::k2: return 0x32;
  case Keys::k3: return 0x33;
  case Keys::k4: return 0x34;
  case Keys::k5: return 0x35;
  case Keys::k6: return 0x36;
  case Keys::k7: return 0x37;
  case Keys::k8: return 0x38;
  case Keys::k9: return 0x39;
  case Keys::kSemicolon: return 0; // FIXME
  case Keys::kEqual: return 0; // FIXME
  case Keys::kA: return 0x41;
  case Keys::kB: return 0x42;
  case Keys::kC: return 0x43;
  case Keys::kD: return 0x44;
  case Keys::kE: return 0x45;
  case Keys::kF: return 0x46;
  case Keys::kG: return 0x47;
  case Keys::kH: return 0x48;
  case Keys::kI: return 0x49;
  case Keys::kJ: return 0x4A;
  case Keys::kK: return 0x4B;
  case Keys::kL: return 0x4C;
  case Keys::kM: return 0x4D;
  case Keys::kN: return 0x4E;
  case Keys::kO: return 0x4F;
  case Keys::kP: return 0x50;
  case Keys::kQ: return 0x51;
  case Keys::kR: return 0x52;
  case Keys::kS: return 0x53;
  case Keys::kT: return 0x54;
  case Keys::kU: return 0x55;
  case Keys::kV: return 0x56;
  case Keys::kW: return 0x57;
  case Keys::kX: return 0x58;
  case Keys::kY: return 0x59;
  case Keys::kZ: return 0x5A;
  case Keys::kLeftBracket: return 0; // FIXME
  case Keys::kBackslash: return 0; // FIXME
  case Keys::kRightBracket: return 0; // FIXME
  case Keys::kGraveAccent: return 0; // FIXME
  case Keys::kEscape: return VK_ESCAPE;
  case Keys::kEnter: return VK_RETURN;
  case Keys::kTab: return VK_TAB;
  case Keys::kBackspace: return VK_BACK;
  case Keys::kInsert: return VK_INSERT;
  case Keys::kDelete: return VK_DELETE;
  case Keys::kRight: return VK_RIGHT;
  case Keys::kLeft: return VK_LEFT;
  case Keys::kDown: return VK_DOWN;
  case Keys::kUp: return VK_UP;
  case Keys::kPageUp: return VK_PRIOR;
  case Keys::kPageDown: return VK_NEXT;
  case Keys::kHome: return VK_HOME;
  case Keys::kEnd: return VK_END;
  case Keys::kCapsLock: return 0; // FIXME
  case Keys::kScrollLock: return 0; // FIXME
  case Keys::kNumLock: return 0; // FIXME
  case Keys::kPrintScreen: return 0; // FIXME
  case Keys::kPause: return 0; // FIXME
  case Keys::kF1: return VK_F1;
  case Keys::kF2: return VK_F2;
  case Keys::kF3: return VK_F3;
  case Keys::kF4: return VK_F4;
  case Keys::kF5: return VK_F5;
  case Keys::kF6: return VK_F6;
  case Keys::kF7: return VK_F7;
  case Keys::kF8: return VK_F8;
  case Keys::kF9: return VK_F9;
  case Keys::kF10: return VK_F10;
  case Keys::kF11: return VK_F11;
  case Keys::kF12: return VK_F12;
  case Keys::kF13: return VK_F13;
  case Keys::kF14: return VK_F14;
  case Keys::kF15: return VK_F15;
  case Keys::kF16: return VK_F16;
  case Keys::kF17: return VK_F17;
  case Keys::kF18: return VK_F19;
  case Keys::kF19: return VK_F19;
  case Keys::kF20: return VK_F20;
  case Keys::kF21: return VK_F21;
  case Keys::kF22: return VK_F22;
  case Keys::kF23: return VK_F23;
  case Keys::kF24: return VK_F24;
  case Keys::kKeypad0: return VK_NUMPAD0;
  case Keys::kKeypad1: return VK_NUMPAD1;
  case Keys::kKeypad2: return VK_NUMPAD2;
  case Keys::kKeypad3: return VK_NUMPAD3;
  case Keys::kKeypad4: return VK_NUMPAD4;
  case Keys::kKeypad5: return VK_NUMPAD5;
  case Keys::kKeypad6: return VK_NUMPAD6;
  case Keys::kKeypad7: return VK_NUMPAD7;
  case Keys::kKeypad8: return VK_NUMPAD8;
  case Keys::kKeypad9: return VK_NUMPAD9;
  case Keys::kKeypadDecimal: return VK_DECIMAL;
  case Keys::kKeypadDivide: return VK_DIVIDE;
  case Keys::kKeypadMultiply: return VK_MULTIPLY;
  case Keys::kKeypadSubtract: return VK_SUBTRACT;
  case Keys::kKeypadAdd: return VK_ADD;
  case Keys::kKeypadEnter: return 0; // FIXME
  case Keys::kKeypadEqual: return 0; // FIXME
  case Keys::kLeftShift: return VK_LSHIFT;
  case Keys::kLeftControl: return VK_LCONTROL;
  case Keys::kLeftAlt: return VK_LMENU;
  case Keys::kLeftSuper: return 0; // FIXME
  case Keys::kRightShift: return VK_RSHIFT;
  case Keys::kRightControl: return VK_RCONTROL;
  case Keys::kRightAlt: return VK_RMENU;
  case Keys::kRightSuper: return 0; // FIXME
  case Keys::kMenu: return 0; // FIXME
  default: return 0; //FIXME
  }
} // KeysToKeycode

} // namespace iris::wsi

tl::expected<std::unique_ptr<iris::wsi::Window::Impl>, std::exception>
iris::wsi::Window::Impl::Create(gsl::czstring<> title, Offset2D offset,
                                Extent2D extent, Options const& options, int) {
  IRIS_LOG_ENTER();
  std::unique_ptr<Impl> pWin;

  try {
    pWin = std::make_unique<Impl>();
  } catch (std::bad_alloc const&) {
    GetLogger()->critical("Cannot allocate memory");
    IRIS_LOG_LEAVE();
    std::terminate();
  } catch (std::exception const& e) {
    GetLogger()->critical("Unhandled exception from std::make_unique<Impl>");
    IRIS_LOG_LEAVE();
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

  for (std::size_t i = 0; i < Keyset::kMaxKeys; ++i) {
    pWin->keyLUT_[i] = KeysToKeycode(static_cast<Keys>(i));
  }

  IRIS_LOG_LEAVE();
  return std::move(pWin);
} // iris::wsi::Window::Impl::Create

iris::wsi::Keyset iris::wsi::Window::Impl::KeyboardState() const noexcept {
  Keyset keyboardState;

  BYTE rawState[256];
  if (!::GetKeyboardState(rawState)) {
    GetLogger()->error("Cannot get keyboard state: {}", ::GetLastError());
    return keyboardState;
  }

  for (std::size_t i = 0; i < Keyset::kMaxKeys; ++i) {
    keyboardState[static_cast<Keys>(i)] = (rawState[keyLUT_[i]] & 0x80);
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

  case WM_LBUTTONDOWN: buttons_[Buttons::kButtonLeft] = true; break;
  case WM_RBUTTONDOWN: buttons_[Buttons::kButtonRight] = true; break;
  case WM_MBUTTONDOWN: buttons_[Buttons::kButtonMiddle] = true; break;
  case WM_XBUTTONDOWN:
    switch(GET_XBUTTON_WPARAM(wParam)) {
    case XBUTTON1: buttons_[Buttons::kButton4] = true; break;
    case XBUTTON2: buttons_[Buttons::kButton5] = true; break;
    }
    break;

  case WM_LBUTTONUP: buttons_[Buttons::kButtonLeft] = false; break;
  case WM_RBUTTONUP: buttons_[Buttons::kButtonRight] = false; break;
  case WM_MBUTTONUP: buttons_[Buttons::kButtonMiddle] = false; break;
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

