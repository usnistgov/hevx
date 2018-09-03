/*! \file
 * \brief \ref iris::wsi::Window::Impl definition for X11.
 */
#include "wsi/window_x11.h"
#include "absl/base/macros.h"
#include "config.h"
#include "spdlog/spdlog.h"
#include "wsi/error.h"
#include <cstdint>
#include <exception>
#include <memory>

static std::uint8_t sErrorCode{0};

static int ErrorHandler(::Display*, ::XErrorEvent* ev) noexcept {
  sErrorCode = ev->error_code;
  return 0;
}

static void GrabErrorHandler(::Display*) noexcept {
  sErrorCode = Success;
  ::XSetErrorHandler(ErrorHandler);
}

static void ReleaseErrorHandler(::Display* disp) noexcept {
  ::XSync(disp, False);
  ::XSetErrorHandler(nullptr);
}

namespace iris::wsi {

static Keys TranslateKeySym(::KeySym keysym) {
  using namespace iris::wsi;

  switch (keysym) {
  case XK_BackSpace: return Keys::kBackspace;
  case XK_Tab: return Keys::kTab;
  case XK_Return: return Keys::kEnter;
  case XK_Pause: return Keys::kPause;
  case XK_Scroll_Lock: return Keys::kScrollLock;
  case XK_Escape: return Keys::kEscape;
  case XK_Delete: return Keys::kDelete;

  case XK_Home: return Keys::kHome;
  case XK_Left: return Keys::kLeft;
  case XK_Up: return Keys::kUp;
  case XK_Down: return Keys::kDown;
  case XK_Page_Up: return Keys::kPageUp;
  case XK_Page_Down: return Keys::kPageDown;
  case XK_End: return Keys::kEnd;

  case XK_Insert: return Keys::kInsert;
  case XK_Num_Lock: return Keys::kNumLock;

  case XK_KP_Enter: return Keys::kKeypadEnter;
  case XK_KP_Home: return Keys::kKeypad7;
  case XK_KP_Left: return Keys::kKeypad4;
  case XK_KP_Up: return Keys::kKeypad8;
  case XK_KP_Right: return Keys::kKeypad6;
  case XK_KP_Down: return Keys::kKeypad2;
  case XK_KP_Page_Up: return Keys::kKeypad9;
  case XK_KP_Page_Down: return Keys::kKeypad3;
  case XK_KP_End: return Keys::kKeypad1;
  case XK_KP_Insert: return Keys::kKeypad0;
  case XK_KP_Delete: return Keys::kKeypadDecimal;
  case XK_KP_Multiply: return Keys::kKeypadMultiply;
  case XK_KP_Add: return Keys::kKeypadAdd;
  case XK_KP_Subtract: return Keys::kKeypadSubtract;
  case XK_KP_Divide: return Keys::kKeypadDivide;

  case XK_F1: return Keys::kF1;
  case XK_F2: return Keys::kF2;
  case XK_F3: return Keys::kF3;
  case XK_F4: return Keys::kF4;
  case XK_F5: return Keys::kF5;
  case XK_F6: return Keys::kF6;
  case XK_F7: return Keys::kF7;
  case XK_F8: return Keys::kF8;
  case XK_F9: return Keys::kF9;
  case XK_F10: return Keys::kF10;
  case XK_F11: return Keys::kF11;
  case XK_F12: return Keys::kF12;
  case XK_F13: return Keys::kF13;
  case XK_F14: return Keys::kF14;
  case XK_F15: return Keys::kF15;
  case XK_F16: return Keys::kF16;
  case XK_F17: return Keys::kF17;
  case XK_F18: return Keys::kF18;
  case XK_F19: return Keys::kF19;
  case XK_F20: return Keys::kF20;
  case XK_F21: return Keys::kF21;
  case XK_F22: return Keys::kF22;
  case XK_F23: return Keys::kF23;
  case XK_F24: return Keys::kF24;
  case XK_F25: return Keys::kF25;

  case XK_Shift_L: return Keys::kLeftShift;
  case XK_Shift_R: return Keys::kRightShift;
  case XK_Control_L: return Keys::kLeftControl;
  case XK_Control_R: return Keys::kRightControl;
  case XK_Caps_Lock: return Keys::kCapsLock;
  case XK_Meta_L: return Keys::kLeftAlt;
  case XK_Meta_R: return Keys::kRightAlt;
  case XK_Super_L: return Keys::kLeftSuper;
  case XK_Super_R: return Keys::kRightSuper;

  case XK_space: return Keys::kSpace;
  case XK_apostrophe: return Keys::kApostrophe;
  case XK_comma: return Keys::kComma;
  case XK_minus: return Keys::kMinus;
  case XK_period: return Keys::kPeriod;
  case XK_slash: return Keys::kSlash;
  case XK_0: return Keys::k0;
  case XK_1: return Keys::k1;
  case XK_2: return Keys::k2;
  case XK_3: return Keys::k3;
  case XK_4: return Keys::k4;
  case XK_5: return Keys::k5;
  case XK_6: return Keys::k6;
  case XK_7: return Keys::k7;
  case XK_8: return Keys::k8;
  case XK_9: return Keys::k9;
  case XK_semicolon: return Keys::kSemicolon;
  case XK_equal: return Keys::kEqual;
  case XK_A: return Keys::kA;
  case XK_B: return Keys::kB;
  case XK_C: return Keys::kC;
  case XK_D: return Keys::kD;
  case XK_E: return Keys::kE;
  case XK_F: return Keys::kF;
  case XK_G: return Keys::kG;
  case XK_H: return Keys::kH;
  case XK_I: return Keys::kI;
  case XK_J: return Keys::kJ;
  case XK_K: return Keys::kK;
  case XK_L: return Keys::kL;
  case XK_M: return Keys::kM;
  case XK_N: return Keys::kN;
  case XK_O: return Keys::kO;
  case XK_P: return Keys::kP;
  case XK_Q: return Keys::kQ;
  case XK_R: return Keys::kR;
  case XK_S: return Keys::kS;
  case XK_T: return Keys::kT;
  case XK_U: return Keys::kU;
  case XK_V: return Keys::kV;
  case XK_W: return Keys::kW;
  case XK_X: return Keys::kX;
  case XK_Y: return Keys::kY;
  case XK_Z: return Keys::kZ;
  case XK_bracketleft: return Keys::kLeftBracket;
  case XK_backslash: return Keys::kBackslash;
  case XK_bracketright: return Keys::kRightBracket;
  case XK_grave: return Keys::kGraveAccent;
  }

  return Keys::kUnknown;
} // TranslateKeySym

static spdlog::logger* sGetLogger() noexcept {
  static std::shared_ptr<spdlog::logger> sLogger = spdlog::get("iris");
  return sLogger.get();
}

} // namespace iris::wsi

tl::expected<std::unique_ptr<iris::wsi::Window::Impl>, std::error_code>
iris::wsi::Window::Impl::Create(gsl::czstring<> title, glm::uvec2 extent,
                                Options const& options) noexcept {
  IRIS_LOG_ENTER(sGetLogger());

  auto pWin = std::make_unique<Impl>();
  if (!pWin) {
    sGetLogger()->error("Unable to allocate memory");
    std::terminate();
  }

  pWin->extent_ = std::move(extent);

  pWin->handle_.display = ::XOpenDisplay("");
  if (!pWin->handle_.display) {
    sGetLogger()->error("Cannot open default display");
    IRIS_LOG_LEAVE(sGetLogger());
    return tl::unexpected(Error::kNoDisplay);
  }

  int minKeycode, maxKeycode, numKeycodes;
  ::XDisplayKeycodes(pWin->handle_.display, &minKeycode, &maxKeycode);
  ::KeySym* keySyms = ::XGetKeyboardMapping(
    pWin->handle_.display, minKeycode, maxKeycode - minKeycode, &numKeycodes);

  for (std::size_t i = 0; i < Keyset::kMaxKeys; ++i) {
    pWin->keyLUT_[i] = TranslateKeySym(keySyms[(i - minKeycode) * numKeycodes]);
  }
  ::XFree(keySyms);

  int const screen = DefaultScreen(pWin->handle_.display);
  pWin->visual_ = DefaultVisual(pWin->handle_.display, screen);

  XSetWindowAttributes attrs = {};
  attrs.border_pixel = 0;
  attrs.colormap = DefaultColormap(pWin->handle_.display, screen);
  attrs.event_mask = StructureNotifyMask | KeyPressMask | KeyReleaseMask |
                     ButtonPressMask | ButtonReleaseMask | ExposureMask |
                     VisibilityChangeMask | PropertyChangeMask;

  GrabErrorHandler(pWin->handle_.display);
  pWin->handle_.window = ::XCreateWindow(
    pWin->handle_.display, DefaultRootWindow(pWin->handle_.display),
    pWin->offset_[0], pWin->offset_[1], pWin->extent_[0], pWin->extent_[1], 0,
    DefaultDepth(pWin->handle_.display, screen), InputOutput, pWin->visual_,
    CWBorderPixel | CWColormap | CWEventMask, &attrs);
  ReleaseErrorHandler(pWin->handle_.display);

  if (sErrorCode != Success) {
    char str[1024];
    ::XGetErrorText(pWin->handle_.display, sErrorCode, str,
                    ABSL_ARRAYSIZE(str));
    sGetLogger()->error("Cannot create window: {}", str);
    IRIS_LOG_LEAVE(sGetLogger());
    return tl::unexpected(Error::kXError);
  }

  for (int i = 0; i < kNumAtoms; ++i) {
    pWin->atoms_[i] = ::XInternAtom(pWin->handle_.display,
                                    AtomToString(static_cast<Atoms>(i)), False);
  }

  ::XSetWMProtocols(pWin->handle_.display, pWin->handle_.window,
                    &pWin->atoms_[WM_DELETE_WINDOW], 1);

  if ((options & Options::kDecorated) != Options::kDecorated) {
    // remove decorations
    struct {
      unsigned long flags;
      unsigned long functions;
      unsigned long decorations;
      long input_mode;
      unsigned long status;
    } hints{2, 0, 0, 0, 0};

    ::XChangeProperty(pWin->handle_.display, pWin->handle_.window,
                      pWin->atoms_[_MOTIF_WM_HINTS],
                      pWin->atoms_[_MOTIF_WM_HINTS], 32, PropModeReplace,
                      reinterpret_cast<unsigned char*>(&hints),
                      sizeof(hints) / sizeof(long));
  }

  ::XWMHints* wmHints = ::XAllocWMHints();
  wmHints->flags = StateHint;
  wmHints->initial_state = NormalState;
  ::XSetWMHints(pWin->handle_.display, pWin->handle_.window, wmHints);
  ::XFree(wmHints);

  ::XSizeHints* szHints = ::XAllocSizeHints();
  if ((options & Options::kSizeable) != Options::kSizeable) {
    // remove sizeability
    szHints->flags |= (PMinSize | PMaxSize);
    szHints->min_width = szHints->max_width = pWin->extent_[0];
    szHints->min_height = szHints->max_height = pWin->extent_[1];
  }

  szHints->flags |= PWinGravity;
  szHints->win_gravity = StaticGravity;
  ::XSetWMNormalHints(pWin->handle_.display, pWin->handle_.window, szHints);
  ::XFree(szHints);

  pWin->Retitle(title);
  IRIS_LOG_LEAVE(sGetLogger());
  return std::move(pWin);
} // iris::wsi::Window::Impl::Create

void iris::wsi::Window::Impl::Dispatch(::XEvent const& event) noexcept {
  switch (event.type) {

  case KeyPress:
    if (event.xkey.window != handle_.window) break;
    keys_.set(keyLUT_[event.xkey.keycode]);
    break;

  case KeyRelease:
    if (event.xkey.window != handle_.window) break;
    if (::XEventsQueued(handle_.display, QueuedAfterReading)) {
      ::XEvent next;
      ::XPeekEvent(handle_.display, &next);
      if (next.type == KeyPress && next.xkey.window == handle_.window &&
          next.xkey.time == event.xkey.time &&
          next.xkey.keycode == event.xkey.keycode) {}
      // Next event is a KeyPress with identical window, time, and keycode;
      // thus key wasn't physically released, so consume next event now.
      ::XNextEvent(handle_.display, &next);
      break;
    }

    keys_.reset(keyLUT_[event.xkey.keycode]);
    break;

  case ButtonPress:
    if (event.xbutton.window != handle_.window) break;
    switch (event.xbutton.button) {
    case Button1: buttons_.set(Buttons::k1); break;
    case Button2: buttons_.set(Buttons::k2); break;
    case Button3: buttons_.set(Buttons::k3); break;
    // case Button4: scroll_ += 1; break;
    // case Button5: scroll_ -= 1; break;
    default:
      buttons_.set(
        static_cast<enum Buttons>(event.xbutton.button - Button1 - 4));
      break;
    }
    break;

  case ButtonRelease:
    if (event.xbutton.window != handle_.window) break;
    switch (event.xbutton.button) {
    case Button1: buttons_.reset(Buttons::k1); break;
    case Button2: buttons_.reset(Buttons::k2); break;
    case Button3: buttons_.reset(Buttons::k3); break;
    default:
      buttons_.reset(
        static_cast<enum Buttons>(event.xbutton.button - Button1 - 4));
      break;
    }
    break;

  case ClientMessage:
    if (event.xclient.message_type == None) break;
    if (event.xclient.message_type != atoms_[WM_PROTOCOLS]) break;
    if (event.xclient.data.l[0] == None) break;

    closed_ =
      (static_cast<Atom>(event.xclient.data.l[0]) == atoms_[WM_DELETE_WINDOW]);
    if (closed_) {
      closeDelegate_();
      ::XDestroyWindow(handle_.display, handle_.window);
    }
    break;

  case ConfigureNotify:
    if (event.xconfigure.window != handle_.window) break;
    if (extent_[0] == static_cast<unsigned int>(event.xconfigure.width) &&
        extent_[1] == static_cast<unsigned int>(event.xconfigure.height)) {
      if (offset_[0] == static_cast<unsigned int>(event.xconfigure.x) &&
          offset_[1] == static_cast<unsigned int>(event.xconfigure.y)) {
        break;
      }
      offset_ = {event.xconfigure.x, event.xconfigure.y};
      moveDelegate_(offset_);
    } else {
      extent_ = {event.xconfigure.width, event.xconfigure.height};
      resizeDelegate_(extent_);
    }
    break;

  default: break;
  }
} // iris::wsi::Window::Impl::Dispatch

gsl::czstring<> iris::wsi::Window::Impl::AtomToString(::Atom atom) noexcept {
  switch (atom) {
#define STR(r)                                                                 \
  case r: return #r
    STR(WM_PROTOCOLS);
    STR(WM_DELETE_WINDOW);
    STR(NET_WM_NAME);
    STR(NET_WM_ICON_NAME);
    STR(_MOTIF_WM_HINTS);
#undef STR
  case iris::wsi::Window::Impl::kNumAtoms: break;
  }

  return "UNKNOWN";
} // iris::wsi::Window::Impl::AtomToString

