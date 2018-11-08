/*! \file
 * \brief \ref iris::wsi::Window::Impl definition for X11.
 */
#include "wsi/window_x11.h"
#include "absl/base/macros.h"
#include "fmt/format.h"
#include "logging.h"
#include <X11/keysym.h>
#include <cstdint>
#include <exception>
#include <memory>

namespace iris::wsi {

//! \brief Implements a std::error_category for \ref X errors
class XCategory : public std::error_category {
public:
  virtual ~XCategory() {}

  //! \brief Get the name of this category.
  virtual const char* name() const noexcept override {
    return "iris::wsi::XCategory";
  }

  //! \brief Convert an int representing an ErrorCodes into a std::string.
  virtual std::string message(int code) const override {
    return fmt::format("{}", code);
  }
}; // class ErrorCodesCategory

//! The global instance of the ErrorCodesCategory.
inline XCategory const gXCategory;

/*! \brief Get the global instance of the ErrorCodesCategory.
 * \return \ref gErrorCodesCategory
 */
inline std::error_category const& GetXCategory() {
  return gXCategory;
}

static ::xcb_keycode_t KeysToKeyCode(::xcb_connection_t* conn,
                                     Keys key) noexcept {
  auto cookie = ::xcb_get_keyboard_mapping(conn, 8, 255 - 8);
  ::xcb_generic_error_t* error;
  auto reply = ::xcb_get_keyboard_mapping_reply(conn, cookie, &error);

  if (error) {
    GetLogger()->error("Cannot get keyboard mapping: {}", error->error_code);
    std::free(error);
    return XCB_NONE;
  }

  ::xcb_keysym_t* keysyms = ::xcb_get_keyboard_mapping_keysyms(reply);
  std::uint8_t const keysyms_per_keycode = reply->keysyms_per_keycode;

  auto findKeySym = [&keysyms, &keysyms_per_keycode](::xcb_keysym_t sym) {
    for (long int i = 0; i < 255 - 8; ++i) {
      for (std::uint8_t j = 0; j < keysyms_per_keycode; ++j) {
        if (keysyms[i * keysyms_per_keycode + j] == sym) {
          return i;
        }
      }
    }
    return XCB_NONE;
  };

  switch (key) {
  case Keys::kSpace: return findKeySym(XK_space);
  case Keys::kApostrophe: return findKeySym(XK_apostrophe);
  case Keys::kComma: return findKeySym(XK_comma);
  case Keys::kMinus: return findKeySym(XK_minus);
  case Keys::kPeriod: return findKeySym(XK_period);
  case Keys::kSlash: return findKeySym(XK_slash);
  case Keys::k0: return findKeySym(XK_0);
  case Keys::k1: return findKeySym(XK_1);
  case Keys::k2: return findKeySym(XK_2);
  case Keys::k3: return findKeySym(XK_3);
  case Keys::k4: return findKeySym(XK_4);
  case Keys::k5: return findKeySym(XK_5);
  case Keys::k6: return findKeySym(XK_6);
  case Keys::k7: return findKeySym(XK_7);
  case Keys::k8: return findKeySym(XK_8);
  case Keys::k9: return findKeySym(XK_9);
  case Keys::kSemicolon: return findKeySym(XK_semicolon);
  case Keys::kEqual: return findKeySym(XK_equal);
  case Keys::kA: return findKeySym(XK_a);
  case Keys::kB: return findKeySym(XK_b);
  case Keys::kC: return findKeySym(XK_c);
  case Keys::kD: return findKeySym(XK_d);
  case Keys::kE: return findKeySym(XK_e);
  case Keys::kF: return findKeySym(XK_f);
  case Keys::kG: return findKeySym(XK_g);
  case Keys::kH: return findKeySym(XK_h);
  case Keys::kI: return findKeySym(XK_i);
  case Keys::kJ: return findKeySym(XK_j);
  case Keys::kK: return findKeySym(XK_k);
  case Keys::kL: return findKeySym(XK_l);
  case Keys::kM: return findKeySym(XK_m);
  case Keys::kN: return findKeySym(XK_n);
  case Keys::kO: return findKeySym(XK_o);
  case Keys::kP: return findKeySym(XK_p);
  case Keys::kQ: return findKeySym(XK_q);
  case Keys::kR: return findKeySym(XK_r);
  case Keys::kS: return findKeySym(XK_s);
  case Keys::kT: return findKeySym(XK_t);
  case Keys::kU: return findKeySym(XK_u);
  case Keys::kV: return findKeySym(XK_v);
  case Keys::kW: return findKeySym(XK_w);
  case Keys::kX: return findKeySym(XK_x);
  case Keys::kY: return findKeySym(XK_y);
  case Keys::kZ: return findKeySym(XK_z);
  case Keys::kLeftBracket: return findKeySym(XK_bracketleft);
  case Keys::kBackslash: return findKeySym(XK_backslash);
  case Keys::kRightBracket: return findKeySym(XK_bracketright);
  case Keys::kGraveAccent: return findKeySym(XK_grave);
  case Keys::kEscape: return findKeySym(XK_Escape);
  case Keys::kEnter: return findKeySym(XK_Return);
  case Keys::kTab: return findKeySym(XK_Tab);
  case Keys::kBackspace: return findKeySym(XK_BackSpace);
  case Keys::kInsert: return findKeySym(XK_Insert); // FIXME: check
  case Keys::kDelete: return findKeySym(XK_Delete); // FIXME: check
  case Keys::kRight: return findKeySym(XK_Right);
  case Keys::kLeft: return findKeySym(XK_Left);
  case Keys::kDown: return findKeySym(XK_Down);
  case Keys::kUp: return findKeySym(XK_Up);
  case Keys::kPageUp: return findKeySym(XK_Page_Up);
  case Keys::kPageDown: return findKeySym(XK_Page_Down);
  case Keys::kHome: return findKeySym(XK_Home);
  case Keys::kEnd: return findKeySym(XK_End);
  case Keys::kCapsLock: return findKeySym(XK_Caps_Lock);
  case Keys::kScrollLock: return findKeySym(XK_Scroll_Lock);
  case Keys::kNumLock: return findKeySym(XK_Num_Lock);
  case Keys::kPrintScreen: return findKeySym(XK_Sys_Req);
  case Keys::kPause: return findKeySym(XK_Break);
  case Keys::kF1: return findKeySym(XK_F1);
  case Keys::kF2: return findKeySym(XK_F2);
  case Keys::kF3: return findKeySym(XK_F3);
  case Keys::kF4: return findKeySym(XK_F4);
  case Keys::kF5: return findKeySym(XK_F5);
  case Keys::kF6: return findKeySym(XK_F6);
  case Keys::kF7: return findKeySym(XK_F7);
  case Keys::kF8: return findKeySym(XK_F8);
  case Keys::kF9: return findKeySym(XK_F9);
  case Keys::kF10: return findKeySym(XK_F10);
  case Keys::kF11: return findKeySym(XK_F11);
  case Keys::kF12: return findKeySym(XK_F12);
  case Keys::kF13: return findKeySym(XK_F13);
  case Keys::kF14: return findKeySym(XK_F14);
  case Keys::kF15: return findKeySym(XK_F15);
  case Keys::kF16: return findKeySym(XK_F16);
  case Keys::kF17: return findKeySym(XK_F17);
  case Keys::kF18: return findKeySym(XK_F18);
  case Keys::kF19: return findKeySym(XK_F19);
  case Keys::kF20: return findKeySym(XK_F20);
  case Keys::kF21: return findKeySym(XK_F21);
  case Keys::kF22: return findKeySym(XK_F22);
  case Keys::kF23: return findKeySym(XK_F23);
  case Keys::kF24: return findKeySym(XK_F24);
  case Keys::kKeypad0: return findKeySym(XK_KP_0);
  case Keys::kKeypad1: return findKeySym(XK_KP_1);
  case Keys::kKeypad2: return findKeySym(XK_KP_2);
  case Keys::kKeypad3: return findKeySym(XK_KP_3);
  case Keys::kKeypad4: return findKeySym(XK_KP_4);
  case Keys::kKeypad5: return findKeySym(XK_KP_5);
  case Keys::kKeypad6: return findKeySym(XK_KP_6);
  case Keys::kKeypad7: return findKeySym(XK_KP_7);
  case Keys::kKeypad8: return findKeySym(XK_KP_8);
  case Keys::kKeypad9: return findKeySym(XK_KP_9);
  case Keys::kKeypadDecimal: return findKeySym(XK_KP_Decimal);
  case Keys::kKeypadDivide: return findKeySym(XK_KP_Divide);
  case Keys::kKeypadMultiply: return findKeySym(XK_KP_Multiply);
  case Keys::kKeypadSubtract: return findKeySym(XK_KP_Subtract);
  case Keys::kKeypadAdd: return findKeySym(XK_KP_Add);
  case Keys::kKeypadEnter: return findKeySym(XK_KP_Enter);
  case Keys::kKeypadEqual: return findKeySym(XK_KP_Equal);
  case Keys::kLeftShift: return findKeySym(XK_Shift_L);
  case Keys::kLeftControl: return findKeySym(XK_Control_L);
  case Keys::kLeftAlt: return findKeySym(XK_Alt_L);
  case Keys::kLeftSuper: return findKeySym(XK_Super_L);
  case Keys::kRightShift: return findKeySym(XK_Shift_R);
  case Keys::kRightControl: return findKeySym(XK_Control_R);
  case Keys::kRightAlt: return findKeySym(XK_Alt_R);
  case Keys::kRightSuper: return findKeySym(XK_Super_R);
  case Keys::kMenu: return findKeySym(XK_Menu);
  default: return XCB_NONE; // FIXME
  }
} // KeysToKeyCode

} // namespace iris::wsi

tl::expected<std::unique_ptr<iris::wsi::Window::Impl>, std::exception>
iris::wsi::Window::Impl::Create(gsl::czstring<> title, Offset2D offset,
                                Extent2D extent, Options const& options[[maybe_unused]],
                                int display) noexcept {
  IRIS_LOG_ENTER();
  std::unique_ptr<iris::wsi::Window::Impl> pWin;

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

  std::string const displayName = fmt::format(":0.{}", display);
  GetLogger()->debug("Opening display {}", displayName);

  pWin->handle_.connection = ::xcb_connect(displayName.c_str(), nullptr);
  if (int error = ::xcb_connection_has_error(pWin->handle_.connection) > 0) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(std::error_code(error, GetXCategory()),
                        "Cannot open display connection"));
  }

  // Get the first screen on the display
  auto conn = pWin->handle_.connection;
  xcb_screen_t* screen = ::xcb_setup_roots_iterator(::xcb_get_setup(conn)).data;

  std::uint32_t mask = XCB_CW_EVENT_MASK;
  std::uint32_t maskValues[1];
  maskValues[0] = XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_KEY_RELEASE |
                  XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE |
                  XCB_EVENT_MASK_STRUCTURE_NOTIFY;

  pWin->handle_.window = ::xcb_generate_id(conn);
  auto win = pWin->handle_.window;

  auto windowCookie =
    ::xcb_create_window_checked(conn,                 // connection
                                XCB_COPY_FROM_PARENT, // depth (same as root)
                                win,                  // window id
                                screen->root,         // parent window
                                offset.x, offset.y, extent.width, extent.height,
                                0,                             // border width
                                XCB_WINDOW_CLASS_INPUT_OUTPUT, // window class
                                screen->root_visual,           // visual
                                mask, maskValues);

  absl::FixedArray<::xcb_intern_atom_cookie_t> atomCookies(kNumAtoms);
  atomCookies[WM_NAME] = ::xcb_intern_atom(conn, 0, 7, "WM_NAME");
  atomCookies[WM_ICON_NAME] = ::xcb_intern_atom(conn, 0, 11, "WM_ICON_NAME");
  atomCookies[WM_PROTOCOLS] = ::xcb_intern_atom(conn, 0, 12, "WM_PROTOCOLS");
  atomCookies[WM_DELETE_WINDOW] =
    ::xcb_intern_atom(conn, 0, 16, "WM_DELETE_WINDOW");
  atomCookies[_MOTIF_WM_HINTS] =
    ::xcb_intern_atom(conn, 0, 16, "_MOTIF_WM_HINTS");

  if (auto error = ::xcb_request_check(conn, windowCookie)) {
    auto const errorCode = error->error_code;
    std::free(error);
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(
      std::error_code(errorCode, GetXCategory()), "Cannot create window"));
  }

  auto getAtomReply =
    [&conn](auto cookie) -> tl::expected<::xcb_atom_t, std::error_code> {
    ::xcb_generic_error_t* error{nullptr};
    if (auto reply = ::xcb_intern_atom_reply(conn, cookie, &error)) {
      ::xcb_atom_t atom = reply->atom;
      std::free(reply);
      return atom;
    }

    auto const errorCode = error->error_code;
    std::free(error);
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::error_code(errorCode, GetXCategory()));
  };

  if (auto atom = getAtomReply(atomCookies[WM_NAME])) {
    pWin->atoms_[WM_NAME] = *atom;
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(atom.error(), "Cannot intern WM_NAME atom"));
  }

  if (auto atom = getAtomReply(atomCookies[WM_ICON_NAME])) {
    pWin->atoms_[WM_ICON_NAME] = *atom;
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(atom.error(), "Cannot intern WM_ICON_NAME atom"));
  }

  if (auto atom = getAtomReply(atomCookies[WM_PROTOCOLS])) {
    pWin->atoms_[WM_PROTOCOLS] = *atom;
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(atom.error(), "Cannot intern WM_PROTOCOLS atom"));
  }

  if (auto atom = getAtomReply(atomCookies[WM_DELETE_WINDOW])) {
    pWin->atoms_[WM_DELETE_WINDOW] = *atom;
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(atom.error(), "Cannot intern WM_DELETE_WINDOW atom"));
  }

  if (auto atom = getAtomReply(atomCookies[_MOTIF_WM_HINTS])) {
    pWin->atoms_[_MOTIF_WM_HINTS] = *atom;
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(atom.error(), "Cannot intern _MOTIF_WM_HINTS atom"));
  }

  auto protocolsCookie =
    ::xcb_change_property_checked(conn, XCB_PROP_MODE_REPLACE, win,
                                  pWin->atoms_[WM_PROTOCOLS], // property
                                  XCB_ATOM_ATOM,              // type
                                  32,                         // format
                                  1,                          // data_len
                                  &pWin->atoms_[WM_DELETE_WINDOW]);

  if (auto error = ::xcb_request_check(conn, protocolsCookie)) {
    auto const errorCode = error->error_code;
    std::free(error);
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(std::error_code(errorCode, GetXCategory()),
                        "Cannot set WM_PROTOCOLS/WM_DELETE_WINDOW property"));
  }

  if ((options & Options::kDecorated) != Options::kDecorated) {
    // remove decorations
    GetLogger()->debug("Removing decorations on {}", title);
    struct {
      unsigned long flags;
      unsigned long functions;
      unsigned long decorations;
      long input_mode;
      unsigned long status;
    } hints{2, 0, 0, 0, 0};

    auto decorationsCookie = ::xcb_change_property_checked(
      conn, XCB_PROP_MODE_REPLACE, win,
      pWin->atoms_[_MOTIF_WM_HINTS],         // property
      pWin->atoms_[_MOTIF_WM_HINTS],         // type
      32,                                    // format
      sizeof(hints) / sizeof(std::uint32_t), // data_len
      reinterpret_cast<unsigned char*>(&hints));

    if (auto error = ::xcb_request_check(conn, decorationsCookie)) {
      auto const errorCode = error->error_code;
      std::free(error);
      IRIS_LOG_LEAVE();
      return tl::unexpected(
        std::system_error(std::error_code(errorCode, GetXCategory()),
                          "Cannot set (no) window decorations property"));
    }
  }

  if ((options & Options::kSizeable) != Options::kSizeable) {
    // remove resizeability
    GetLogger()->debug("Removing resizeability on {}", title);

    xcb_size_hints_t sizeHints = {};
    sizeHints.max_width = sizeHints.min_width = extent.width;
    sizeHints.max_height = sizeHints.min_height = extent.height;
    sizeHints.flags =
      XCB_ICCCM_SIZE_HINT_P_MIN_SIZE | XCB_ICCCM_SIZE_HINT_P_MAX_SIZE;

    auto normalHintsCookie =
      xcb_icccm_set_wm_normal_hints_checked(conn, win, &sizeHints);

    if (auto error = ::xcb_request_check(conn, normalHintsCookie)) {
      auto const errorCode = error->error_code;
      std::free(error);
      IRIS_LOG_LEAVE();
      return tl::unexpected(
        std::system_error(std::error_code(errorCode, GetXCategory()),
                          "Cannot set window size hints"));
    }
  }

  //
  // TODO: get the key lut
  //

  pWin->Retitle(title);

  pWin->rect_.offset = std::move(offset);
  pWin->rect_.extent = std::move(extent);

  for (std::size_t i = 0; i < Keyset::kMaxKeys; ++i) {
    pWin->keyLUT_[i] = KeysToKeyCode(conn, static_cast<Keys>(i));
  }

  IRIS_LOG_LEAVE();
  return std::move(pWin);
}

iris::wsi::Keyset iris::wsi::Window::Impl::KeyboardState() const noexcept {
  Keyset keyboardState;

  auto cookie = ::xcb_query_keymap(handle_.connection);
  xcb_generic_error_t* error;
  auto keymap = ::xcb_query_keymap_reply(handle_.connection, cookie, &error);

  if (error) {
    GetLogger()->error("Cannot get keyboard state: {}", error->error_code);
    std::free(error);
    return keyboardState;
  }

  for (std::size_t i = 0; i < Keyset::kMaxKeys; ++i) {
    ::xcb_keycode_t code = keyLUT_[i];
    if (static_cast<Keys>(i) == Keys::kEscape ||
        static_cast<Keys>(i) == Keys::kA) {
    }
    keyboardState[static_cast<Keys>(i)] =
      ((keymap->keys[(code / 8) + 1] & (1 << (code % 8))) != 0);
  }

  std::free(keymap);
  return keyboardState;
} // iris::wsi::Window::Impl::KeyboardState

glm::uvec2 iris::wsi::Window::Impl::CursorPos() const noexcept {
  return glm::vec2(0, 0);
  //::Window root, child;
  //glm::ivec2 rootPos, childPos;
  //unsigned int mask;
  //::XQueryPointer(handle_.display, handle_.window, &root, &child, &rootPos[0],
                  //&rootPos[1], &childPos[0], &childPos[1], &mask);
  //return childPos;
} // iris::wsi::Window::Impl::CursorPos

iris::wsi::Window::Impl::~Impl() noexcept {
  IRIS_LOG_ENTER();
  xcb_disconnect(handle_.connection);
  IRIS_LOG_LEAVE();
} // iris::wsi::Window::Impl::~Impl

void iris::wsi::Window::Impl::Dispatch(
  gsl::not_null<::xcb_generic_event_t*> event) noexcept {
  switch (event->response_type & ~0x80) {
  case XCB_KEY_PRESS: break;
  case XCB_KEY_RELEASE: break;

  case XCB_BUTTON_PRESS: {
    auto ev = reinterpret_cast<::xcb_button_press_event_t*>(event.get());
    if (ev->event != handle_.window) break;
    switch(ev->detail) {
      case 1: buttons_[Buttons::kLeft] = true; break;
      case 2: buttons_[Buttons::kMiddle] = true; break;
      case 3: buttons_[Buttons::kRight] = true; break;
    }
  } break;

  case XCB_BUTTON_RELEASE: {
    auto ev = reinterpret_cast<::xcb_button_release_event_t*>(event.get());
    if (ev->event != handle_.window) break;
    switch(ev->detail) {
      case 1: buttons_[Buttons::kLeft] = false; break;
      case 2: buttons_[Buttons::kMiddle] = false; break;
      case 3: buttons_[Buttons::kRight] = false; break;
      case 4: scroll_.y += 1.f; break;
      case 5: scroll_.y -= 1.f; break;
    }
  } break;

  case XCB_CLIENT_MESSAGE: {
    auto ev = reinterpret_cast<::xcb_client_message_event_t*>(event.get());
    if (ev->type != atoms_[WM_PROTOCOLS]) break;
    if (ev->data.data32[0] == atoms_[WM_DELETE_WINDOW]) Close();
  } break;

  case XCB_CONFIGURE_NOTIFY: {
    auto ev = reinterpret_cast<::xcb_configure_notify_event_t*>(event.get());
    if (ev->window != handle_.window) break;
    if (rect_.extent.width == ev->width && rect_.extent.height == ev->height) {
      if (rect_.offset.x != ev->x || rect_.offset.y != ev->y) {
        rect_.offset = Offset2D(ev->x, ev->y);
        moveDelegate_(rect_.offset);
      }
    } else {
      rect_.extent = Extent2D(ev->width, ev->height);
      resizeDelegate_(rect_.extent);
    }
  } break;
  }
} // iris::wsi::Window::Impl::Dispatch

