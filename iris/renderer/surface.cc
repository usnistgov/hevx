#include "renderer/surface.h"
#include "renderer/impl.h"
#if defined(VK_USE_PLATFORM_XLIB_KHR)
#include "wsi/window_x11.h"
#elif defined(VK_USE_PLATFORM_WIN32_KHR)
#include "wsi/window_win32.h"
#endif
#if PLATFORM_COMPILER_MSVC
#pragma warning(push)
#pragma warning(disable : 4127)
#endif
#include "spdlog/spdlog.h"
#if PLATFORM_COMPILER_MSVC
#pragma warning(pop)
#endif

namespace iris::Renderer {

static spdlog::logger* sGetLogger() noexcept {
  static std::shared_ptr<spdlog::logger> sLogger = spdlog::get("iris");
  return sLogger.get();
}

tl::expected<VkSurfaceKHR, std::error_code>
CreateSurface(wsi::Window& window) noexcept {
  IRIS_LOG_ENTER(sGetLogger());
  VkSurfaceKHR surface;
  auto native = window.NativeHandle();

#if defined(VK_USE_PLATFORM_XLIB_KHR)

  VkXlibSurfaceCreateInfoKHR sci = {};
  sci.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
  sci.dpy = native.display;
  sci.window = native.window;

  VkResult result = vkCreateXlibSurfaceKHR(sInstance, &sci, nullptr, &surface);
  if (result != VK_SUCCESS) {
    sGetLogger()->error("Cannot create surface: {}", to_string(result));
    IRIS_LOG_LEAVE(sGetLogger());
    return tl::unexpected(Error::kSurfaceCreationFailed);
  }

#elif defined(VK_USE_PLATFORM_WIN32_KHR)

  VkWin32SurfaceCreateInfoKHR sci = {};
  sci.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
  sci.hinstance = native.hInstance;
  sci.hwnd = native.hWnd;

  VkResult result = vkCreateWin32SurfaceKHR(sInstance, &sci, nullptr, &surface);
  if (result != VK_SUCCESS) {
    sGetLogger()->error("Cannot create surface: {}", to_string(result));
    IRIS_LOG_LEAVE(sGetLogger());
    return tl::unexpected(make_error_code(result));
  }

#endif

  IRIS_LOG_LEAVE(sGetLogger());
  return surface;
} // CreateSurface

tl::expected<bool, std::error_code>
CheckSurfaceSupport(VkSurfaceKHR surface) noexcept {
  IRIS_LOG_ENTER(sGetLogger());
  VkBool32 support;
  VkResult result = vkGetPhysicalDeviceSurfaceSupportKHR(
    sPhysicalDevice, sGraphicsQueueFamilyIndex, surface, &support);
  if (result != VK_SUCCESS) {
    sGetLogger()->error("Cannot check for physical device surface support: {}",
                        to_string(result));
    IRIS_LOG_LEAVE(sGetLogger());
    return tl::unexpected(make_error_code(result));
  }

  IRIS_LOG_LEAVE(sGetLogger());
  return (support == VK_TRUE);
} // CheckSurfaceSupport

} // namespace iris::Renderer

tl::expected<iris::Renderer::Surface, std::error_code>
iris::Renderer::Surface::Create(wsi::Window& window) noexcept {
  IRIS_LOG_ENTER(sGetLogger());

  Surface surface;

  if (auto sfc = CreateSurface(window); !sfc) {
    IRIS_LOG_LEAVE(sGetLogger());
    return tl::unexpected(sfc.error());
  } else {
    surface.handle = *sfc;
  }

  if (auto chk = CheckSurfaceSupport(surface.handle); !chk) {
    IRIS_LOG_LEAVE(sGetLogger());
    return tl::unexpected(chk.error());
  } else if (!*chk) {
    sGetLogger()->error("Surface is not supported by the physical device.");
    IRIS_LOG_LEAVE(sGetLogger());
    return tl::unexpected(Error::kSurfaceNotSupported);
  }

  IRIS_LOG_LEAVE(sGetLogger());
  return surface;
} // iris::Renderer::Surface::Create
