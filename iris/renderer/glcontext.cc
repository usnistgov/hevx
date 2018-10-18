#include "renderer/glcontext.h"
#include "absl/base/macros.h"
#include "config.h"
#include "error.h"
#include "logging.h"
#if defined(VK_USE_PLATFORM_XLIB_KHR)
#include "wsi/window_x11.h"
#elif defined(VK_USE_PLATFORM_WIN32_KHR)
#include "wsi/window_win32.h"
#endif

#if defined(VK_USE_PLATFORM_XLIB_KHR)

struct iris::Renderer::GLContext::Impl {
}; // struct iris::Renderer::GLContext::Impl

#elif defined(VK_USE_PLATFORM_WIN32_KHR)

struct iris::Renderer::GLContext::Impl {
  ::HDC hDC{0};
  ::HGLRC handle{0};
}; // struct iris::Renderer::GLContext::Impl

#endif

tl::expected<iris::Renderer::GLContext, std::error_code>
iris::Renderer::GLContext::Create(wsi::Window& window) noexcept {
  IRIS_LOG_ENTER();
  auto native = window.NativeHandle();

  try {
    GLContext context;
    context.pImpl_ = std::make_unique<Impl>();

#if defined(VK_USE_PLATFORM_XLIB_KHR)
#elif defined(VK_USE_PLATFORM_WIN32_KHR)

  PIXELFORMATDESCRIPTOR pfd = {sizeof(PIXELFORMATDESCRIPTOR),
                               1,
                               PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL |
                                 PFD_DOUBLEBUFFER,
                               PFD_TYPE_RGBA,
                               32, // colorbuffer bits
                               0,
                               0,
                               0,
                               0,
                               0,
                               0,
                               0,
                               0,
                               0,
                               0,
                               0,
                               0,
                               0,
                               32, // depthbuffer bits
                               0,
                               0,
                               PFD_MAIN_PLANE,
                               0,
                               0,
                               0,
                               0};

  context.pImpl_->hDC = GetDC(native.hWnd);
  ::SetPixelFormat(context.pImpl_->hDC, 1, &pfd);
  context.pImpl_->handle = ::wglCreateContext(context.pImpl_->hDC);

  if (!context.pImpl_->handle) {
    char str[1024];
    ::FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, NULL, ::GetLastError(),
                     MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), str,
                     ABSL_ARRAYSIZE(str), NULL);
    GetLogger()->error("Cannot create OpenGL context: {}", str);
    IRIS_LOG_LEAVE();
    return tl::unexpected(Error::kGLContextCreationFailed);
  }

#endif

    IRIS_LOG_LEAVE();
    return std::move(context);
  } catch (std::exception const& e) {
    GetLogger()->error("Exception while creating context: {}", e.what());
    IRIS_LOG_LEAVE();
    return tl::unexpected(Error::kGLContextCreationFailed);
  }
}

void iris::Renderer::GLContext::MakeCurrent() noexcept {
#if defined(VK_USE_PLATFORM_XLIB_KHR)
#elif defined(VK_USE_PLATFORM_WIN32_KHR)
  ::wglMakeCurrent(pImpl_->hDC, pImpl_->handle);
#endif
} // iris::Renderer::GLContext::MakeCurrent

iris::Renderer::GLContext::GLContext() noexcept = default;
iris::Renderer::GLContext::GLContext(GLContext&&) noexcept = default;
iris::Renderer::GLContext& iris::Renderer::GLContext::operator=(GLContext&&) noexcept = default;

iris::Renderer::GLContext::~GLContext() noexcept {
  IRIS_LOG_ENTER();
  if (!pImpl_) {
    IRIS_LOG_LEAVE();
    return;
  }

#if defined(VK_USE_PLATFORM_XLIB_KHR)
#elif defined(VK_USE_PLATFORM_WIN32_KHR)
  ::wglDeleteContext(pImpl_->handle);
#endif

  IRIS_LOG_LEAVE();
}
