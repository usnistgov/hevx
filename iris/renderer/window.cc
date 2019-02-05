#include "renderer/window.h"
#include "config.h"
#include "logging.h"
#include "wsi/input.h"
#include <functional>

tl::expected<iris::Renderer::Window, std::exception>
iris::Renderer::Window::Create(gsl::czstring<> title, wsi::Offset2D offset,
                               wsi::Extent2D extent,
                               glm::vec4 const& clearColor[[maybe_unused]],
                               Options const& options, int display,
                               std::size_t numFrames) noexcept {
  IRIS_LOG_ENTER();

  wsi::PlatformWindow::Options platformOptions =
    wsi::PlatformWindow::Options::kSizeable;
  if ((options & Options::kDecorated) == Options::kDecorated) {
    platformOptions |= wsi::PlatformWindow::Options::kDecorated;
  }

  Window window(numFrames);
  if (auto win =
        wsi::PlatformWindow::Create(title, std::move(offset), std::move(extent),
                                    platformOptions, display)) {
    window.platformWindow = std::move(*win);
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(win.error());
  }

  window.showUI = (options & Options::kShowUI) == Options::kShowUI;

  window.uiContext_.reset(ImGui::CreateContext());
  ImGui::SetCurrentContext(window.uiContext_.get());
  ImGui::StyleColorsDark();

  ImGuiIO& io = ImGui::GetIO();

  io.KeyMap[ImGuiKey_Tab] = static_cast<int>(wsi::Keys::kTab);
  io.KeyMap[ImGuiKey_LeftArrow] = static_cast<int>(wsi::Keys::kLeft);
  io.KeyMap[ImGuiKey_RightArrow] = static_cast<int>(wsi::Keys::kRight);
  io.KeyMap[ImGuiKey_UpArrow] = static_cast<int>(wsi::Keys::kUp);
  io.KeyMap[ImGuiKey_DownArrow] = static_cast<int>(wsi::Keys::kDown);
  io.KeyMap[ImGuiKey_PageUp] = static_cast<int>(wsi::Keys::kPageUp);
  io.KeyMap[ImGuiKey_PageDown] = static_cast<int>(wsi::Keys::kPageDown);
  io.KeyMap[ImGuiKey_Home] = static_cast<int>(wsi::Keys::kHome);
  io.KeyMap[ImGuiKey_End] = static_cast<int>(wsi::Keys::kEnd);
  io.KeyMap[ImGuiKey_Insert] = static_cast<int>(wsi::Keys::kInsert);
  io.KeyMap[ImGuiKey_Delete] = static_cast<int>(wsi::Keys::kDelete);
  io.KeyMap[ImGuiKey_Backspace] = static_cast<int>(wsi::Keys::kBackspace);
  io.KeyMap[ImGuiKey_Space] = static_cast<int>(wsi::Keys::kSpace);
  io.KeyMap[ImGuiKey_Enter] = static_cast<int>(wsi::Keys::kEnter);
  io.KeyMap[ImGuiKey_Escape] = static_cast<int>(wsi::Keys::kEscape);
  io.KeyMap[ImGuiKey_A] = static_cast<int>(wsi::Keys::kA);
  io.KeyMap[ImGuiKey_C] = static_cast<int>(wsi::Keys::kC);
  io.KeyMap[ImGuiKey_V] = static_cast<int>(wsi::Keys::kV);
  io.KeyMap[ImGuiKey_X] = static_cast<int>(wsi::Keys::kX);
  io.KeyMap[ImGuiKey_Y] = static_cast<int>(wsi::Keys::kY);
  io.KeyMap[ImGuiKey_Z] = static_cast<int>(wsi::Keys::kZ);

  window.platformWindow.Show();
  window.platformWindow.OnResize(
    std::bind(&Window::Resize, &window, std::placeholders::_1));
  window.platformWindow.OnClose(std::bind(&Window::Close, &window));

  IRIS_LOG_LEAVE();
  return std::move(window);
} // iris::Renderer::Window::Create

iris::Renderer::Window::Window(Window&& other) noexcept
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
  , colorTarget(other.colorTarget)
  , depthStencilTarget(other.depthStencilTarget)
  , frames_(std::move(other.frames_))
  , frameIndex_(other.frameIndex_)
  , imageAcquired_(other.imageAcquired_)
  , uiContext_(std::move(other.uiContext_)) {
  // Re-bind delegates
  platformWindow.OnResize(
    std::bind(&Window::Resize, this, std::placeholders::_1));
  platformWindow.OnClose(std::bind(&Window::Close, this));
} // iris::Renderer::Window::Window

iris::Renderer::Window& iris::Renderer::Window::
operator=(Window&& rhs) noexcept {
  Expects(colorImages.size() == rhs.colorImages.size());
  Expects(colorImageViews.size() == rhs.colorImageViews.size());
  Expects(frames_.size() == rhs.frames_.size());

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
  colorTarget = rhs.colorTarget;
  depthStencilTarget = rhs.depthStencilTarget;
  std::copy_n(rhs.frames_.begin(), frames_.size(), frames_.begin());
  frameIndex_ = rhs.frameIndex_;
  imageAcquired_ = rhs.imageAcquired_;
  uiContext_ = std::move(rhs.uiContext_);

  // Re-bind delegates
  platformWindow.OnResize(
    std::bind(&Window::Resize, this, std::placeholders::_1));
  platformWindow.OnClose(std::bind(&Window::Close, this));

  return *this;
} // iris::Renderer::Window::operator=
