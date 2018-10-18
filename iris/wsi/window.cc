#include "wsi/window.h"
#include "config.h"
#include "logging.h"
#include "wsi/error.h"
#if defined(VK_USE_PLATFORM_XLIB_KHR)
#include "wsi/window_x11.h"
#elif defined(VK_USE_PLATFORM_WIN32_KHR)
#include "wsi/window_win32.h"
#endif
/*! \file
 * \brief \ref iris::wsi::Window definition.
 */

tl::expected<iris::wsi::Window, std::error_code>
iris::wsi::Window::Create(gsl::czstring<> title, Rect rect,
                          Options const& options, int display) noexcept {
  try {
    Window window;
    if (auto pImpl = Impl::Create(title, std::move(rect), options, display)) {
      window.pImpl_ = std::move(*pImpl);
    } else {
      return tl::unexpected(pImpl.error());
    }

    return std::move(window);
  } catch (std::exception const& e) {
    GetLogger()->error("Exception while creating window: {}", e.what());
    return tl::unexpected(Error::kCreateFailed);
  }
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

iris::wsi::Window::~Window() noexcept {
  IRIS_LOG_ENTER();
  IRIS_LOG_LEAVE();
}
