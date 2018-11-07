#include "wsi/window.h"
#include "config.h"
#include "logging.h"
#include "wsi/error_codes.h"
#if defined(PLATFORM_LINUX)
#include "wsi/window_x11.h"
#elif defined(PLATFORM_WINDOWS)
#include "wsi/window_win32.h"
#endif
/*! \file
 * \brief \ref iris::wsi::Window definition.
 */

tl::expected<iris::wsi::Window, std::exception>
iris::wsi::Window::Create(gsl::czstring<> title, Offset2D offset,
                          Extent2D extent, Options const& options,
                          int display) noexcept {
  Window window;
  if (auto pImpl = Impl::Create(title, std::move(offset), std::move(extent),
                                options, display)) {
    window.pImpl_ = std::move(*pImpl);
  } else {
    return tl::unexpected(pImpl.error());
  }

  return std::move(window);
}

iris::wsi::Rect2D iris::wsi::Window::Rect() const noexcept {
  return pImpl_->Rect();
} // iris::wsi::Window::Rect

iris::wsi::Offset2D iris::wsi::Window::Offset() const noexcept {
  return pImpl_->Offset();
} // iris::wsi::Window::Offset

iris::wsi::Extent2D iris::wsi::Window::Extent() const noexcept {
  return pImpl_->Extent();
} // iris::wsi::Window::Extent

iris::wsi::Keyset iris::wsi::Window::Keys() const noexcept {
  return pImpl_->Keys();
} // iris::wsi::Window::Keys

iris::wsi::Buttonset iris::wsi::Window::Buttons() const noexcept {
  return pImpl_->Buttons();
} // iris::wsi::Window::Buttons

glm::uvec2 iris::wsi::Window::CursorPos() const noexcept {
  return pImpl_->CursorPos();
}

std::string iris::wsi::Window::Title() const noexcept {
  return pImpl_->Title();
}

void iris::wsi::Window::Retitle(gsl::czstring<> title) noexcept {
  pImpl_->Retitle(title);
}

void iris::wsi::Window::Move(Offset2D const& offset) {
  pImpl_->Move(offset);
}

void iris::wsi::Window::Resize(Extent2D const& extent) {
  pImpl_->Resize(extent);
}

bool iris::wsi::Window::IsClosed() const noexcept {
  return pImpl_->IsClosed();
}

void iris::wsi::Window::Close() const noexcept {
  pImpl_->Close();
}

void iris::wsi::Window::Show() const noexcept {
  pImpl_->Show();
}

void iris::wsi::Window::Hide() const noexcept {
  pImpl_->Hide();
}

bool iris::wsi::Window::IsFocused() const noexcept {
  return pImpl_->IsFocused();
}

void iris::wsi::Window::PollEvents() noexcept {
  pImpl_->PollEvents();
}

void iris::wsi::Window::OnClose(CloseDelegate delegate) noexcept {
  pImpl_->OnClose(delegate);
}

void iris::wsi::Window::OnMove(MoveDelegate delegate) noexcept {
  pImpl_->OnMove(delegate);
}

void iris::wsi::Window::OnResize(ResizeDelegate delegate) noexcept {
  pImpl_->OnResize(delegate);
}

iris::wsi::Window::NativeHandle_t iris::wsi::Window::NativeHandle() const
  noexcept {
  return pImpl_->NativeHandle();
}

iris::wsi::Window::Window() noexcept = default;
iris::wsi::Window::Window(Window&&) noexcept = default;
iris::wsi::Window& iris::wsi::Window::operator=(Window&&) noexcept = default;

iris::wsi::Window::~Window() noexcept {
  IRIS_LOG_ENTER();
  IRIS_LOG_LEAVE();
}

