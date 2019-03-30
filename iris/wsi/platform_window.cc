/*! \file
\brief \ref iris::wsi::PlatformWindow definition.
*/
#include "wsi/platform_window.h"
#include "config.h"
#include "logging.h"
#if PLATFORM_LINUX
#include "wsi/platform_window_x11.h"
#elif PLATFORM_WINDOWS
#include "wsi/platform_window_win32.h"
#endif

tl::expected<iris::wsi::PlatformWindow, std::exception>
iris::wsi::PlatformWindow::Create(gsl::czstring<> title, Offset2D offset,
                                  Extent2D extent, Options const& options,
                                  int display) noexcept {
  PlatformWindow window;
  if (auto pImpl = Impl::Create(title, std::move(offset), std::move(extent),
                                options, display)) {
    window.pImpl_ = std::move(*pImpl);
  } else {
    return tl::unexpected(pImpl.error());
  }

  return std::move(window);
}

iris::wsi::Rect2D iris::wsi::PlatformWindow::Rect() const noexcept {
  return pImpl_->Rect();
}

iris::wsi::Offset2D iris::wsi::PlatformWindow::Offset() const noexcept {
  return pImpl_->Offset();
}

iris::wsi::Extent2D iris::wsi::PlatformWindow::Extent() const noexcept {
  return pImpl_->Extent();
}

glm::vec2 iris::wsi::PlatformWindow::CursorPos() const noexcept {
  return pImpl_->CursorPos();
}

void iris::wsi::PlatformWindow::Retitle(gsl::czstring<> title) noexcept {
  pImpl_->Retitle(title);
}

void iris::wsi::PlatformWindow::Move(Offset2D const& offset) {
  pImpl_->Move(offset);
}

void iris::wsi::PlatformWindow::Resize(Extent2D const& extent) {
  pImpl_->Resize(extent);
}

bool iris::wsi::PlatformWindow::IsClosed() const noexcept {
  return pImpl_->IsClosed();
}

void iris::wsi::PlatformWindow::Close() const noexcept {
  pImpl_->Close();
}

void iris::wsi::PlatformWindow::Show() const noexcept {
  pImpl_->Show();
}

void iris::wsi::PlatformWindow::Hide() const noexcept {
  pImpl_->Hide();
}

bool iris::wsi::PlatformWindow::IsFocused() const noexcept {
  return pImpl_->IsFocused();
}

void iris::wsi::PlatformWindow::PollEvents() noexcept {
  pImpl_->PollEvents();
}

void iris::wsi::PlatformWindow::OnClose(CloseDelegate delegate) noexcept {
  pImpl_->OnClose(delegate);
}

void iris::wsi::PlatformWindow::OnMove(MoveDelegate delegate) noexcept {
  pImpl_->OnMove(delegate);
}

void iris::wsi::PlatformWindow::OnResize(ResizeDelegate delegate) noexcept {
  pImpl_->OnResize(delegate);
}

iris::wsi::PlatformWindow::NativeHandle_t
iris::wsi::PlatformWindow::NativeHandle() const noexcept {
  return pImpl_->NativeHandle();
}

iris::wsi::PlatformWindow::PlatformWindow() noexcept = default;
iris::wsi::PlatformWindow::PlatformWindow(PlatformWindow&&) noexcept = default;
iris::wsi::PlatformWindow& iris::wsi::PlatformWindow::
operator=(PlatformWindow&&) noexcept = default;

iris::wsi::PlatformWindow::~PlatformWindow() noexcept {
  if (!pImpl_) return;
  IRIS_LOG_ENTER();
  IRIS_LOG_LEAVE();
}
