/*!
\file
\brief \ref iris::wsi::PlatformWindow::Impl definition for Win32.
*/
#include "wsi/platform_window_win32.h"
#include "absl/base/macros.h"
#include "imgui.h"
#include "logging.h"
#include "wsi/input.h"

namespace iris::wsi {

static Keys KeycodeToKeys(int keycode) noexcept {
  switch (keycode) {
  case VK_SPACE: return Keys::kSpace;
  // case Keys::kApostrophe: return 0; // TODO
  // case Keys::kComma: return 0; // TODO
  // case Keys::kMinus: return 0; // TODO
  // case Keys::kPeriod: return 0; // TODO
  // case Keys::kSlash: return 0; // TODO
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
  // case Keys::kSemicolon: return 0; // TODO
  // case Keys::kEqual: return 0; // TODO
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
  // case Keys::kLeftBracket: return 0; // TODO
  // case Keys::kBackslash: return 0; // TODO
  // case Keys::kRightBracket: return 0; // TODO
  // case Keys::kGraveAccent: return 0; // TODO
  case VK_ESCAPE: return Keys::kEscape;
  case VK_RETURN: return Keys::kEnter;
  case VK_TAB: return Keys::kTab;
  case VK_BACK: return Keys::kBackspace;
  case VK_INSERT: return Keys::kInsert;
  case VK_DELETE: return Keys::kDelete;
  case VK_RIGHT: return Keys::kRight;
  case VK_LEFT: return Keys::kLeft;
  case VK_DOWN: return Keys::kDown;
  case VK_UP: return Keys::kUp;
  case VK_PRIOR: return Keys::kPageUp;
  case VK_NEXT: return Keys::kPageDown;
  case VK_HOME: return Keys::kHome;
  case VK_END: return Keys::kEnd;
  // case Keys::kCapsLock: return 0; // TODO
  // case Keys::kScrollLock: return 0; // TODO
  // case Keys::kNumLock: return 0; // TODO
  // case Keys::kPrintScreen: return 0; // TODO
  // case Keys::kPause: return 0; // TODO
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
  case VK_DECIMAL: return Keys::kKeypadDecimal;
  case VK_DIVIDE: return Keys::kKeypadDivide;
  case VK_MULTIPLY: return Keys::kKeypadMultiply;
  case VK_SUBTRACT: Keys::kKeypadSubtract;
  case VK_ADD: Keys::kKeypadAdd;
  // case Keys::kKeypadEnter: return 0; // TODO
  // case Keys::kKeypadEqual: return 0; // TODO
  // case Keys::kLeftShift: return VK_LSHIFT;
  // case Keys::kLeftControl: return VK_LCONTROL;
  // case Keys::kLeftAlt: return VK_LMENU;
  // case Keys::kLeftSuper: return 0; // TODO
  // case Keys::kRightShift: return VK_RSHIFT;
  // case Keys::kRightControl: return VK_RCONTROL;
  // case Keys::kRightAlt: return VK_RMENU;
  // case Keys::kRightSuper: return 0; // TODO
  // case Keys::kMenu: return 0; // TODO
  default: return Keys::kUnknown; // TODO
  }
} // KeycodeToKeys

} // namespace iris::wsi

tl::expected<std::unique_ptr<iris::wsi::PlatformWindow::Impl>, std::exception>
iris::wsi::PlatformWindow::Impl::Create(gsl::czstring<> title, Offset2D offset,
                                        Extent2D extent, Options const& options,
                                        int) {
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

  for (int i = 0; i < gsl::narrow_cast<int>(pWin->keyLUT_.size()); ++i) {
    pWin->keyLUT_[i] = KeycodeToKeys(i);
  }

  IRIS_LOG_LEAVE();
  return std::move(pWin);
} // iris::wsi::PlatformWindow::Impl::Create

glm::vec2 iris::wsi::PlatformWindow::Impl::CursorPos() const noexcept {
  glm::vec2 cursorPos{-FLT_MAX, -FLT_MAX};

  POINT rawPos;
  if (auto active = ::GetForegroundWindow()) {
    if (active == handle_.hWnd || ::IsChild(active, handle_.hWnd)) {
      if (::GetCursorPos(&rawPos) && ::ScreenToClient(handle_.hWnd, &rawPos)) {
        return {rawPos.x, rawPos.y};
      }
    }
  }

  return {-FLT_MAX, -FLT_MAX};
} // iris::wsi::PlatformWindow::Impl::CursorPos

iris::wsi::PlatformWindow::Impl::~Impl() noexcept {
  IRIS_LOG_ENTER();
  IRIS_LOG_LEAVE();
} // iris::wsi::PlatformWindow::Impl::~Impl

::LRESULT CALLBACK iris::wsi::PlatformWindow::Impl::Dispatch(
  ::UINT uMsg, ::WPARAM wParam, ::LPARAM lParam) noexcept {
  ::LRESULT res = 0;

  switch (uMsg) {
  case WM_ACTIVATE: focused_ = (LOWORD(wParam) == WA_ACTIVE); break;

  case WM_CHAR: break;

  case WM_KEYDOWN:
  case WM_SYSKEYDOWN: ImGui::GetIO().KeysDown[keyLUT_[wParam]] = 1; break;

  case WM_KEYUP:
  case WM_SYSKEYUP: ImGui::GetIO().KeysDown[keyLUT_[wParam]] = 0; break;

  case WM_LBUTTONDOWN:
  case WM_LBUTTONDBLCLK:
  case WM_RBUTTONDOWN:
  case WM_RBUTTONDBLCLK:
  case WM_MBUTTONDOWN:
  case WM_MBUTTONDBLCLK:
  case WM_XBUTTONDOWN:
  case WM_XBUTTONDBLCLK: {
    int button = 0;
    if (uMsg == WM_LBUTTONDOWN || uMsg == WM_LBUTTONDBLCLK) {
      button = kButtonLeft;
    }
    if (uMsg == WM_RBUTTONDOWN || uMsg == WM_RBUTTONDBLCLK) {
      button = kButtonRight;
    }
    if (uMsg == WM_MBUTTONDOWN || uMsg == WM_MBUTTONDBLCLK) {
      button = kButtonMiddle;
    }
    if (uMsg == WM_XBUTTONDOWN || uMsg == WM_XBUTTONDBLCLK) {
      switch (GET_XBUTTON_WPARAM(wParam)) {
      case XBUTTON1: button = 3;
      case XBUTTON2: button = 4;
      }
    }

    if (!ImGui::IsAnyMouseDown() && ::GetCapture() == NULL) {
      ::SetCapture(handle_.hWnd);
    }
    ImGui::GetIO().MouseDown[button] = true;
  } break;

  case WM_LBUTTONUP:
  case WM_RBUTTONUP:
  case WM_MBUTTONUP:
  case WM_XBUTTONUP: {
    int button = 0;
    if (uMsg == WM_LBUTTONUP) button = 0;
    if (uMsg == WM_RBUTTONUP) button = 1;
    if (uMsg == WM_MBUTTONUP) button = 2;
    if (uMsg == WM_XBUTTONUP) {
      switch (GET_XBUTTON_WPARAM(wParam)) {
      case XBUTTON1: button = 3;
      case XBUTTON2: button = 4;
      }
    }

    ImGui::GetIO().MouseDown[button] = false;
    if (!ImGui::IsAnyMouseDown() && ::GetCapture() == handle_.hWnd) {
      ::ReleaseCapture();
    }
  } break;

  case WM_MOUSEWHEEL:
    ImGui::GetIO().MouseWheel +=
      static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) /
      static_cast<float>(WHEEL_DELTA);
    break;

  case WM_MOUSEHWHEEL:
    ImGui::GetIO().MouseWheelH +=
      static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) /
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

  case WM_CLOSE: Close(); break;

  case WM_DESTROY: ::PostQuitMessage(0); break;

  default: res = ::DefWindowProcA(handle_.hWnd, uMsg, wParam, lParam);
  }

  return res;
} // iris::wsi::PlatformWindow::Impl::Dispatch

::WNDCLASSA iris::wsi::PlatformWindow::Impl::sWindowClass = {
  CS_OWNDC | CS_HREDRAW | CS_VREDRAW, &Impl::WndProc, 0, 0, 0, 0, 0, 0, nullptr,
  "HevIrisWsiPlatformWindowClass"}; // iris::wsi::PlatformWindow::Impl::sWindowClass

::LRESULT iris::wsi::PlatformWindow::Impl::WndProc(::HWND hWnd, ::UINT uMsg,
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
} // iris::wsi::PlatformWindow::Impl::WndProc
