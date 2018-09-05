#include "renderer/surface.h"
#include "absl/container/fixed_array.h"
#include "logging.h"
#include "renderer/impl.h"
#if defined(VK_USE_PLATFORM_XLIB_KHR)
#include "wsi/window_x11.h"
#elif defined(VK_USE_PLATFORM_WIN32_KHR)
#include "wsi/window_win32.h"
#endif

namespace iris::Renderer {

tl::expected<VkSurfaceKHR, std::error_code>
static CreateSurface(wsi::Window& window) noexcept {
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
static CheckSurfaceSupport(VkSurfaceKHR surface) noexcept {
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

tl::expected<bool, std::error_code> static CheckSurfaceFormat(
  VkSurfaceKHR surface, VkSurfaceFormatKHR desired) noexcept {
  IRIS_LOG_ENTER(sGetLogger());
  VkResult result;

  std::uint32_t numSurfaceFormats;
  result = vkGetPhysicalDeviceSurfaceFormatsKHR(sPhysicalDevice, surface,
                                                &numSurfaceFormats, nullptr);
  if (result != VK_SUCCESS) {
    sGetLogger()->error("Cannot get physical device surface formats: {}",
                        to_string(result));
    IRIS_LOG_LEAVE(sGetLogger());
    return tl::unexpected(make_error_code(result));
  }

  absl::FixedArray<VkSurfaceFormatKHR> surfaceFormats(numSurfaceFormats);
  result = vkGetPhysicalDeviceSurfaceFormatsKHR(
    sPhysicalDevice, surface, &numSurfaceFormats, surfaceFormats.data());
  if (result != VK_SUCCESS) {
    sGetLogger()->error("Cannot get physical device surface formats: {}",
                        to_string(result));
    IRIS_LOG_LEAVE(sGetLogger());
    return tl::unexpected(make_error_code(result));
  }

  if (numSurfaceFormats == 1 &&
      surfaceFormats[0].format == VK_FORMAT_UNDEFINED) {
    IRIS_LOG_LEAVE(sGetLogger());
    return true;
  }

  for (auto&& supported : surfaceFormats) {
    if (supported.format == desired.format &&
        supported.colorSpace == desired.colorSpace) {
      IRIS_LOG_LEAVE(sGetLogger());
      return true;
    }
  }

  IRIS_LOG_LEAVE(sGetLogger());
  return false;
} // CheckSurfaceFormat

} // namespace iris::Renderer

tl::expected<iris::Renderer::Surface, std::error_code>
iris::Renderer::Surface::Create(wsi::Window& window) noexcept {
  IRIS_LOG_ENTER(sGetLogger());

  Surface surface;

  if (auto sfc = CreateSurface(window)) {
    surface.handle = *sfc;
  } else {
    IRIS_LOG_LEAVE(sGetLogger());
    return tl::unexpected(sfc.error());
  }

  if (auto chk = CheckSurfaceSupport(surface.handle)) {
    if (!*chk) {
      sGetLogger()->error("Surface is not supported by the physical device.");
      IRIS_LOG_LEAVE(sGetLogger());
      return tl::unexpected(Error::kSurfaceNotSupported);
    }
  } else {
    IRIS_LOG_LEAVE(sGetLogger());
    return tl::unexpected(chk.error());
  }

  if (auto chk = CheckSurfaceFormat(surface.handle, sSurfaceColorFormat)) {
    if (!*chk) {
      sGetLogger()->error("Surface format is not supported.");
      IRIS_LOG_LEAVE(sGetLogger());
      return tl::unexpected(Error::kSurfaceNotSupported);
    }
  } else {
    IRIS_LOG_LEAVE(sGetLogger());
    return tl::unexpected(chk.error());
  }

  if (auto error = surface.Resize(window.Extent())) {
    IRIS_LOG_LEAVE(sGetLogger());
    return tl::unexpected(error);
  }

  IRIS_LOG_LEAVE(sGetLogger());
  return surface;
} // iris::Renderer::Surface::Create

std::error_code
iris::Renderer::Surface::Resize(glm::uvec2 const& newExtent) noexcept {
  IRIS_LOG_ENTER(sGetLogger());

  IRIS_LOG_LEAVE(sGetLogger());
  return VulkanResult::kSuccess;
} // iris::Renderer::Surface::Resize

