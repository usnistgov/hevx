#include "window.h"
#include "config.h"
#if defined(VK_USE_PLATFORM_XLIB_KHR)
#include "window_x11.h"
#elif defined(VK_USE_PLATFORM_WIN32_KHR)
#include "window_win32.h"
#endif
/*! \file
 * \brief \ref iris::wsi::Window definition.
 */

tl::expected<iris::wsi::Window, std::error_code>
iris::wsi::Window::Create(gsl::czstring<> title, glm::uvec2 extent,
                          Options const& options) noexcept {
  Window window;
  if (auto pImpl = Impl::Create(title, std::move(extent), options)) {
    window.pImpl_ = std::move(*pImpl);
  } else {
    return tl::make_unexpected(pImpl.error());
  }

  return std::move(window);
}

glm::uvec2 iris::wsi::Window::Offset() const noexcept {
  return pImpl_->Offset();
}

glm::uvec2 iris::wsi::Window::Extent() const noexcept {
  return pImpl_->Extent();
}

iris::wsi::Keyset iris::wsi::Window::Keys() const noexcept {
  return pImpl_->Keys();
}

iris::wsi::Buttonset iris::wsi::Window::Buttons() const noexcept {
  return pImpl_->Buttons();
}

glm::uvec2 iris::wsi::Window::CursorPos() const noexcept {
  return pImpl_->CursorPos();
}

void iris::wsi::Window::Retitle(gsl::czstring<> title) noexcept {
  pImpl_->Retitle(title);
}

void iris::wsi::Window::Move(glm::uvec2 const& offset) {
  pImpl_->Move(offset);
}

void iris::wsi::Window::Resize(glm::uvec2 const& extent) {
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
iris::wsi::Window::~Window() noexcept = default;

