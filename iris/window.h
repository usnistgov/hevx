#ifndef HEV_IRIS_WINDOW_H_
#define HEV_IRIS_WINDOW_H_

#include "absl/container/fixed_array.h"
#include "expected.hpp"
#include "glm/vec4.hpp"
#include "iris/components/renderable.h"
#include "iris/vulkan.h"
#include "iris/wsi/platform_window.h"
#include "imgui.h"
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <system_error>
#include <type_traits>

namespace iris {

struct Window {
  //! \brief Options for window creation.
  enum class Options {
    kNone = (0),           //!< No options.
    kDecorated = (1 << 0), //!< The window has decorations (title bar, borders).
    kSizeable = (1 << 1),  //!< The window is sizeable.
    kStereo = (1 << 2),    //!< The window has stereo output.
    kShowUI = (1 << 3),    //!< The window has UI shown.
  };

  std::string title;
  VkClearColorValue clearColor;
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

  VkImage depthStencilImage;
  VmaAllocation depthStencilImageAllocation;
  VkImageView depthStencilImageView;

  VkImage colorTarget;
  VmaAllocation colorTargetAllocation;
  VkImageView colorTargetView;

  VkImage depthStencilTarget;
  VmaAllocation depthStencilTargetAllocation;
  VkImageView depthStencilTargetView;

  struct Frame {
    VkSemaphore imageAvailable{VK_NULL_HANDLE};
    VkCommandPool commandPool{VK_NULL_HANDLE};
    VkCommandBuffer commandBuffer{VK_NULL_HANDLE};
    VkFramebuffer framebuffer{VK_NULL_HANDLE};
  };

  absl::FixedArray<Frame> frames;
  std::uint32_t frameIndex{0};
  VkSemaphore imageAcquired{VK_NULL_HANDLE};

  std::unique_ptr<ImGuiContext, decltype(&ImGui::DestroyContext)> uiContext;
  Renderer::Component::Renderable uiRenderable;

  [[nodiscard]] Frame& currentFrame() noexcept { return frames[frameIndex]; }
  [[nodiscard]] Frame& previousFrame() noexcept {
    return frames[(frameIndex - 1) % frames.size()];
  }

  Window(std::string windowTitle, VkClearColorValue cc, std::size_t numFrames)
    : title(std::move(windowTitle))
    , clearColor(cc)
    , colorImages(numFrames)
    , colorImageViews(numFrames)
    , frames(numFrames)
    , uiContext(nullptr, &ImGui::DestroyContext) {}

  Window(Window const&) = delete;
  Window(Window&&) noexcept;
  Window& operator=(Window const&) = delete;
  Window& operator=(Window&&) noexcept;
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

inline Window::Window(Window&& other) noexcept
  : title(std::move(other.title))
  , clearColor(other.clearColor)
  , resized(other.resized)
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
  , imageAcquired(other.imageAcquired)
  , uiContext(std::move(other.uiContext)) {} // Window::Window

} // namespace iris

#endif // HEV_IRIS_WINDOW_H_
