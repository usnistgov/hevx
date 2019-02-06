#include "window.h"
#include "config.h"
#include "logging.h"
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
  , depthStencilImage(other.depthStencilImage)
  , depthStencilImageAllocation(other.depthStencilImageAllocation)
  , depthStencilImageView(other.depthStencilImageView)
  , colorTarget(other.colorTarget)
  , colorTargetAllocation(other.colorTargetAllocation)
  , colorTargetView(other.colorTargetView)
  , depthStencilTarget(other.depthStencilTarget)
  , depthStencilTargetAllocation(other.depthStencilTargetAllocation)
  , depthStencilTargetView(other.depthStencilTargetView)
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
  other.depthStencilImage = VK_NULL_HANDLE;
  other.depthStencilImageAllocation = VK_NULL_HANDLE;
  other.depthStencilImageView = VK_NULL_HANDLE;
  other.colorTarget = VK_NULL_HANDLE;
  other.colorTargetAllocation = VK_NULL_HANDLE;
  other.colorTargetView = VK_NULL_HANDLE;
  other.depthStencilTarget = VK_NULL_HANDLE;
  other.depthStencilTargetAllocation = VK_NULL_HANDLE;
  other.depthStencilTargetView = VK_NULL_HANDLE;
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
  depthStencilImage = rhs.depthStencilImage;
  depthStencilImageAllocation = rhs.depthStencilImageAllocation;
  depthStencilImageView = rhs.depthStencilImageView;
  colorTarget = rhs.colorTarget;
  colorTargetAllocation = rhs.colorTargetAllocation;
  colorTargetView = rhs.colorTargetView;
  depthStencilTarget = rhs.depthStencilTarget;
  depthStencilTargetAllocation = rhs.depthStencilTargetAllocation;
  depthStencilTargetView = rhs.depthStencilTargetView;

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
  rhs.depthStencilImage = VK_NULL_HANDLE;
  rhs.depthStencilImageAllocation = VK_NULL_HANDLE;
  rhs.depthStencilImageView = VK_NULL_HANDLE;
  rhs.colorTarget = VK_NULL_HANDLE;
  rhs.colorTargetAllocation = VK_NULL_HANDLE;
  rhs.colorTargetView = VK_NULL_HANDLE;
  rhs.depthStencilTarget = VK_NULL_HANDLE;
  rhs.depthStencilTargetAllocation = VK_NULL_HANDLE;
  rhs.depthStencilTargetView = VK_NULL_HANDLE;

  return *this;
} // iris::Window::operator=
