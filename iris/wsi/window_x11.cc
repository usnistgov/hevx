/*! \file
 * \brief \ref iris::wsi::Window::Impl definition for X11.
 */
#include "wsi/window_x11.h"
#include "fmt/format.h"
#include "imgui.h"
#include "logging.h"
#include "wsi/input.h"
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <memory>
#include <system_error>
#include <type_traits>
#include <utility>
#include <X11/keysym.h>
#include <xcb/xcb_icccm.h>

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

static Keys
KeyCodeToKeys(::xcb_keycode_t keycode,
              ::xcb_get_keyboard_mapping_reply_t* mapping) noexcept {
  auto keysyms = reinterpret_cast<xcb_keysym_t*>(mapping + 1);

  for (int j = 0; j < mapping->keysyms_per_keycode; ++j) {
    xcb_keysym_t keysym = keysyms[j + keycode * mapping->keysyms_per_keycode];
    switch(keysym) {
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
    case XK_a: return Keys::kA;
    case XK_b: return Keys::kB;
    case XK_c: return Keys::kC;
    case XK_d: return Keys::kD;
    case XK_e: return Keys::kE;
    case XK_f: return Keys::kF;
    case XK_g: return Keys::kG;
    case XK_h: return Keys::kH;
    case XK_i: return Keys::kI;
    case XK_j: return Keys::kJ;
    case XK_k: return Keys::kK;
    case XK_l: return Keys::kL;
    case XK_m: return Keys::kM;
    case XK_n: return Keys::kN;
    case XK_o: return Keys::kO;
    case XK_p: return Keys::kP;
    case XK_q: return Keys::kQ;
    case XK_r: return Keys::kR;
    case XK_s: return Keys::kS;
    case XK_t: return Keys::kT;
    case XK_u: return Keys::kU;
    case XK_v: return Keys::kV;
    case XK_w: return Keys::kW;
    case XK_x: return Keys::kX;
    case XK_y: return Keys::kY;
    case XK_z: return Keys::kZ;
    case XK_bracketleft: return Keys::kLeftBracket;
    case XK_backslash: return Keys::kBackslash;
    case XK_bracketright: return Keys::kRightBracket;
    case XK_grave: return Keys::kGraveAccent;
    case XK_Escape: return Keys::kEscape;
    case XK_Return: return Keys::kEnter;
    case XK_Tab: return Keys::kTab;
    case XK_BackSpace: return Keys::kBackspace;
    case XK_Insert: return Keys::kInsert;
    case XK_Delete: return Keys::kDelete;
    case XK_Right: return Keys::kRight;
    case XK_Left: return Keys::kLeft;
    case XK_Down: return Keys::kDown;
    case XK_Up: return Keys::kUp;
    case XK_Page_Up: return Keys::kPageUp;
    case XK_Page_Down: return Keys::kPageDown;
    case XK_Home: return Keys::kHome;
    case XK_End: return Keys::kEnd;
    case XK_Caps_Lock: return Keys::kCapsLock;
    case XK_Scroll_Lock: return Keys::kScrollLock;
    case XK_Num_Lock: return Keys::kNumLock;
    case XK_Sys_Req: return Keys::kPrintScreen;
    case XK_Break: return Keys::kPause;
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
    case XK_KP_0: return Keys::kKeypad0;
    case XK_KP_1: return Keys::kKeypad1;
    case XK_KP_2: return Keys::kKeypad2;
    case XK_KP_3: return Keys::kKeypad3;
    case XK_KP_4: return Keys::kKeypad4;
    case XK_KP_5: return Keys::kKeypad5;
    case XK_KP_6: return Keys::kKeypad6;
    case XK_KP_7: return Keys::kKeypad7;
    case XK_KP_8: return Keys::kKeypad8;
    case XK_KP_9: return Keys::kKeypad9;
    case XK_KP_Decimal: return Keys::kKeypadDecimal;
    case XK_KP_Divide: return Keys::kKeypadDivide;
    case XK_KP_Multiply: return Keys::kKeypadMultiply;
    case XK_KP_Subtract: return Keys::kKeypadSubtract;
    case XK_KP_Add: return Keys::kKeypadAdd;
    case XK_KP_Enter: return Keys::kKeypadEnter;
    case XK_KP_Equal: return Keys::kKeypadEqual;
    case XK_Shift_L: return Keys::kLeftShift;
    case XK_Control_L: return Keys::kLeftControl;
    case XK_Alt_L: return Keys::kLeftAlt;
    case XK_Super_L: return Keys::kLeftSuper;
    case XK_Shift_R: return Keys::kRightShift;
    case XK_Control_R: return Keys::kRightControl;
    case XK_Alt_R: return Keys::kRightAlt;
    case XK_Super_R: return Keys::kRightSuper;
    case XK_Menu: return Keys::kMenu;
    }
  }

  return Keys::kUnknown;
} // KeyCodeToKeys
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

  xcb_setup_t const* setup = xcb_get_setup(conn);

  auto cookie = ::xcb_get_keyboard_mapping(
    conn, setup->min_keycode, setup->max_keycode - setup->min_keycode + 1);
  ::xcb_generic_error_t* error;
  auto mapping = ::xcb_get_keyboard_mapping_reply(conn, cookie, &error);

  if (error) {
    auto const errorCode = error->error_code;
    std::free(error);
    return tl::unexpected(
      std::system_error(std::error_code(errorCode, GetXCategory()),
                        "Cannot get keyboard mapping"));
  }

  int const nKeycodes = mapping->length / mapping->keysyms_per_keycode;

  for (int i = 0; i < nKeycodes; ++i) {
    ::xcb_keycode_t const keycode = setup->min_keycode + i;
    pWin->keyLUT_[keycode] = KeyCodeToKeys(i, mapping);
  }

  IRIS_LOG_LEAVE();
  return std::move(pWin);
}

glm::uvec2 iris::wsi::Window::Impl::CursorPos() const noexcept {
  auto cookie = ::xcb_query_pointer(handle_.connection, handle_.window);
  ::xcb_generic_error_t* error;
  auto pointer = ::xcb_query_pointer_reply(handle_.connection, cookie, &error);

  if (error) {
    std::free(error);
    return glm::uvec2(0, 0);
  }

  glm::uvec2 pos(pointer->win_x, pointer->win_y);
  std::free(pointer);
  return pos;
} // iris::wsi::Window::Impl::CursorPos

iris::wsi::Window::Impl::~Impl() noexcept {
  IRIS_LOG_ENTER();
  xcb_disconnect(handle_.connection);
  IRIS_LOG_LEAVE();
} // iris::wsi::Window::Impl::~Impl

void iris::wsi::Window::Impl::Dispatch(
  gsl::not_null<::xcb_generic_event_t*> event) noexcept {
  ImGuiIO& io = ImGui::GetIO();

  switch (event->response_type & ~0x80) {
  case XCB_KEY_PRESS: {
    auto ev = reinterpret_cast<::xcb_key_press_event_t*>(event.get());
    io.KeysDown[keyLUT_[ev->detail]] = 1;
  } break;

  case XCB_KEY_RELEASE: {
    auto ev = reinterpret_cast<::xcb_key_press_event_t*>(event.get());
    io.KeysDown[keyLUT_[ev->detail]] = 0;
  } break;

  case XCB_BUTTON_PRESS: {
    auto ev = reinterpret_cast<::xcb_button_press_event_t*>(event.get());
    if (ev->event != handle_.window) break;
    int button = 0;
    switch(ev->detail) {
      case 1: button = 0; break;
      case 3: button = 1; break;
      case 2: button = 2; break;
      //case 4: scroll_.y += 1.f; break;
      //case 5: scroll_.y -= 1.f; break;
    }

    io.MouseDown[button] = true;
    // FIXME: need to handle capture
  } break;

  case XCB_BUTTON_RELEASE: {
    auto ev = reinterpret_cast<::xcb_button_release_event_t*>(event.get());
    if (ev->event != handle_.window) break;
    int button = 0;
    switch(ev->detail) {
      case 1: button = 0; break;
      case 3: button = 1; break;
      case 2: button = 2; break;
    }

    // FIXME: need to handle capture
    io.MouseDown[button] = false;
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

