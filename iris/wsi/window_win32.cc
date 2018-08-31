/*! \file
 * \brief \ref iris::wsi::Window::Impl definition for Win32.
 */
#include "wsi/window_win32.h"
#include "config.h"
#pragma warning(push)
#pragma warning(disable: 4127)
#include "spdlog/spdlog.h"
#pragma warning(pop)
#include "wsi/error.h"

namespace iris::wsi {

static spdlog::logger* sGetLogger() noexcept {
  static std::shared_ptr<spdlog::logger> sLogger = spdlog::get("iris");
  return sLogger.get();
}

} // namespace iris::wsi

tl::expected<std::unique_ptr<iris::wsi::Window::Impl>, std::error_code>
iris::wsi::Window::Impl::Create(gsl::czstring<> title, glm::uvec2 extent,
                                Options const& options) noexcept {
  IRIS_LOG_ENTER(sGetLogger());
} // iris::wsi::Window::Impl::Create


::LRESULT CALLBACK iris::wsi::Window::Impl::Dispatch(::UINT uMsg,
                                                     ::WPARAM wParam,
                                                     ::LPARAM lParam) noexcept {
} // iris::wsi::Display
