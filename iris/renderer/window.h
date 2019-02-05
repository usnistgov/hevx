#ifndef HEV_IRIS_RENDERER_WINDOW_H_
#define HEV_IRIS_RENDERER_WINDOW_H_

#include "absl/container/fixed_array.h"
#include "expected.hpp"
#include "glm/vec4.hpp"
#include "iris/renderer/renderer.h"
#include "iris/renderer/vulkan_support.h"
#include "iris/wsi/platform_window.h"
#include "imgui.h"

namespace iris::Renderer {

struct Window {
  //! \brief Options for window creation.
  enum class Options {
    kDecorated = (1 << 0), //!< The window has decorations (title bar, borders).
    kSizeable = (1 << 1),  //!< The window is sizeable.
    kStereo = (1 << 2),    //!< The window has stereo output.
    kShowUI = (1 << 3),    //!< The window has UI shown.
  };

  // forward-declare this so that it can be used below in Create
  friend Options operator|(Options const& lhs, Options const& rhs) noexcept;

  [[nodiscard]] static tl::expected<Window, std::exception>
  Create(gsl::czstring<> title, wsi::Offset2D offset, wsi::Extent2D extent,
         glm::vec4 const& clearColor, Options const& options, int display,
         std::size_t numFrames) noexcept;

  [[nodiscard]] std::system_error Resize(wsi::Extent2D const& newExtent) noexcept;
  void Close() noexcept;

  void BeginFrame(float frameDelta);
  [[nodiscard]] tl::expected<VkCommandBuffer, std::system_error> EndFrame() noexcept;

  bool resized{false};
  bool showUI{false};

  wsi::PlatformWindow platformWindow{};
  VkSurfaceKHR surface{VK_NULL_HANDLE};

  VkExtent2D extent{};
  VkViewport viewport{};
  VkRect2D scissor{};

  VkSwapchainKHR swapchain{VK_NULL_HANDLE};
  absl::FixedArray<VkImage> colorImages;
  absl::FixedArray<VkImageView> colorImageViews;

  Image depthStencilImage;
  Image colorTarget;
  Image depthStencilTarget;

  struct Frame {
    VkFramebuffer framebuffer{VK_NULL_HANDLE};
    VkSemaphore imageAvailable{VK_NULL_HANDLE};
    VkSemaphore renderFinished{VK_NULL_HANDLE};
    VkCommandPool commandPool{VK_NULL_HANDLE};
    VkCommandBuffer commandBuffer{VK_NULL_HANDLE};
    VkFence fence{VK_NULL_HANDLE};
  };

private:
  absl::FixedArray<Frame> frames_;
  std::uint32_t frameIndex_{0};
  VkSemaphore imageAcquired_{VK_NULL_HANDLE};

  std::unique_ptr<ImGuiContext, decltype(&ImGui::DestroyContext)> uiContext_;

public:
  [[nodiscard]] Frame& currentFrame() noexcept { return frames_[frameIndex_]; }
  [[nodiscard]] Frame& previousFrame() noexcept {
    return frames_[(frameIndex_ - 1) % frames_.size()];
  }

  Window(std::size_t numFrames)
    : colorImages(numFrames)
    , colorImageViews(numFrames)
    , frames_(numFrames)
    , uiContext_(nullptr, &ImGui::DestroyContext) {}

  Window(Window const&) = delete;
  Window(Window&& other) noexcept;
  Window& operator=(Window const&) = delete;
  Window& operator=(Window&& other) noexcept;
  ~Window() noexcept = default;
}; // struct Window

//! \brief bit-wise or of \ref Window::Options.
inline Window::Options operator|(Window::Options const& lhs,
                                 Window::Options const& rhs) noexcept {
  using U = std::underlying_type_t<Window::Options>;
  return static_cast<Window::Options>(static_cast<U>(lhs) |
                                      static_cast<U>(rhs));
}

//! \brief bit-wise or of \ref Window::Options.
inline Window::Options operator|=(Window::Options& lhs,
                                  Window::Options const& rhs) noexcept {
  lhs = lhs | rhs;
  return lhs;
}

//! \brief bit-wise and of \ref Window::Options.
inline Window::Options operator&(Window::Options const& lhs,
                                 Window::Options const& rhs) noexcept {
  using U = std::underlying_type_t<Window::Options>;
  return static_cast<Window::Options>(static_cast<U>(lhs) &
                                      static_cast<U>(rhs));
}

//! \brief bit-wise and of \ref Window::Options.
inline Window::Options operator&=(Window::Options& lhs,
                                  Window::Options const& rhs) noexcept {
  lhs = lhs & rhs;
  return lhs;
}

} // namespace iris::Renderer

#endif // HEV_IRIS_RENDERER_WINDOW_H_
