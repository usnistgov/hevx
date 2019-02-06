#include "window.h"
#include "config.h"
#include "logging.h"
#include "vulkan_support.h"
#include "wsi/input.h"
#include <functional>

iris::Window::Window(Window&& other) noexcept
  : resized(other.resized)
  , showUI(other.showUI)
  , platformWindow(std::move(other.platformWindow))
  , surface(other.surface)
  , extent(other.extent)
  , viewport(other.viewport)
  , scissor(other.scissor)
  , swapchain(other.swapchain)
  , colorImages(std::move(other.colorImages))
  , colorImageViews(std::move(other.colorImageViews))
  , depthStencilImage(std::move(other.depthStencilImage))
  , colorTarget(std::move(other.colorTarget))
  , depthStencilTarget(std::move(other.depthStencilTarget))
  , frames(std::move(other.frames))
  , frameIndex(other.frameIndex)
  , uiContext(std::move(other.uiContext)) {
  // Re-bind delegates
  platformWindow.OnResize(
    std::bind(&Window::Resize, this, std::placeholders::_1));
  platformWindow.OnClose(std::bind(&Window::Close, this));

  // Nullify other
  other.surface = VK_NULL_HANDLE;
  other.swapchain = VK_NULL_HANDLE;
} // iris::Window::Window

iris::Window& iris::Window::operator=(Window&& rhs) noexcept {
  Expects(colorImages.size() == rhs.colorImages.size());
  Expects(colorImageViews.size() == rhs.colorImageViews.size());
  Expects(frames.size() == rhs.frames.size());

  if (this == &rhs) return *this;

  resized = rhs.resized;
  showUI = rhs.showUI;
  platformWindow = std::move(rhs.platformWindow);
  surface = rhs.surface;
  extent = rhs.extent;
  viewport = rhs.viewport;
  scissor = rhs.scissor;
  swapchain = rhs.swapchain;
  std::copy_n(rhs.colorImages.begin(), colorImages.size(), colorImages.begin());
  std::copy_n(rhs.colorImageViews.begin(), colorImageViews.size(),
              colorImageViews.begin());
  depthStencilImage = std::move(rhs.depthStencilImage);
  colorTarget = std::move(rhs.colorTarget);
  depthStencilTarget = std::move(rhs.depthStencilTarget);
  std::copy_n(rhs.frames.begin(), frames.size(), frames.begin());
  frameIndex = rhs.frameIndex;
  uiContext = std::move(rhs.uiContext);

  // Re-bind delegates
  platformWindow.OnResize(
    std::bind(&Window::Resize, this, std::placeholders::_1));
  platformWindow.OnClose(std::bind(&Window::Close, this));

  // Nullify rhs
  rhs.surface = VK_NULL_HANDLE;
  rhs.swapchain = VK_NULL_HANDLE;

  return *this;
} // iris::Window::operator=
