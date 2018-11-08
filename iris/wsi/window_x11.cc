/*! \file
 * \brief \ref iris::wsi::Window::Impl definition for X11.
 */
#include "wsi/window_x11.h"
#include "absl/base/macros.h"
#include "fmt/format.h"
#include "logging.h"
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
    std::terminate();
  } catch (std::exception const& e) {
    GetLogger()->critical("Unhandled exception from std::make_unique<Impl>");
    return tl::unexpected(e);
  }

  std::string const displayName = fmt::format(":0.{}", display);
  GetLogger()->debug("Opening display {}", displayName);

  pWin->handle_.connection = ::xcb_connect(displayName.c_str(), nullptr);
  if (int error = ::xcb_connection_has_error(pWin->handle_.connection) > 0) {
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
    return tl::unexpected(std::error_code(errorCode, GetXCategory()));
  };

  if (auto atom = getAtomReply(atomCookies[WM_NAME])) {
    pWin->atoms_[WM_NAME] = *atom;
  } else {
    return tl::unexpected(
      std::system_error(atom.error(), "Cannot intern WM_NAME atom"));
  }

  if (auto atom = getAtomReply(atomCookies[WM_ICON_NAME])) {
    pWin->atoms_[WM_ICON_NAME] = *atom;
  } else {
    return tl::unexpected(
      std::system_error(atom.error(), "Cannot intern WM_ICON_NAME atom"));
  }

  if (auto atom = getAtomReply(atomCookies[WM_PROTOCOLS])) {
    pWin->atoms_[WM_PROTOCOLS] = *atom;
  } else {
    return tl::unexpected(
      std::system_error(atom.error(), "Cannot intern WM_PROTOCOLS atom"));
  }

  if (auto atom = getAtomReply(atomCookies[WM_DELETE_WINDOW])) {
    pWin->atoms_[WM_DELETE_WINDOW] = *atom;
  } else {
    return tl::unexpected(
      std::system_error(atom.error(), "Cannot intern WM_DELETE_WINDOW atom"));
  }

  if (auto atom = getAtomReply(atomCookies[_MOTIF_WM_HINTS])) {
    pWin->atoms_[_MOTIF_WM_HINTS] = *atom;
  } else {
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
      return tl::unexpected(
        std::system_error(std::error_code(errorCode, GetXCategory()),
                          "Cannot set window size hints"));
    }
  }

  //
  // TODO: get the key lut
  //

  pWin->rect_.offset = std::move(offset);
  pWin->rect_.extent = std::move(extent);

  pWin->Retitle(title);

  IRIS_LOG_LEAVE();
  return std::move(pWin);
}

iris::wsi::Window::Impl::~Impl() noexcept {
  IRIS_LOG_ENTER();
  xcb_disconnect(handle_.connection);
  IRIS_LOG_LEAVE();
} // iris::wsi::Window::Impl::~Impl

void iris::wsi::Window::Impl::Dispatch(
  gsl::not_null<::xcb_generic_event_t*> event) noexcept {
  switch (event->response_type & ~0x80) {
  case XCB_KEY_PRESS: {
    auto ev = reinterpret_cast<::xcb_key_press_event_t*>(event.get());
    if (ev->event != handle_.window) break;
    GetLogger()->debug("KEY_PRESS: {:x} state: {}", ev->detail, ev->state);
  } break;

  case XCB_KEY_RELEASE: {
    auto ev = reinterpret_cast<::xcb_key_release_event_t*>(event.get());
    if (ev->event != handle_.window) break;
    GetLogger()->debug("KEY_RELEASE: {:x} state: {}", ev->detail, ev->state);
  } break;

  case XCB_BUTTON_PRESS: {
    auto ev = reinterpret_cast<::xcb_button_press_event_t*>(event.get());
    if (ev->event != handle_.window) break;
    GetLogger()->debug("BUTTON_PRESS: {:x} state: {}", ev->detail, ev->state);
  } break;

  case XCB_BUTTON_RELEASE: {
    auto ev = reinterpret_cast<::xcb_button_release_event_t*>(event.get());
    if (ev->event != handle_.window) break;
    GetLogger()->debug("BUTTON_RELEASE: {:x} state: {}", ev->detail, ev->state);
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

