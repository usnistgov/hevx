/*!
\file
\brief iris::Window declaration
*/
#ifndef HEV_IRIS_WINDOW_H_
#define HEV_IRIS_WINDOW_H_

#include "absl/container/fixed_array.h"
#include "expected.hpp"
#include "glm/mat4x4.hpp"
#include "glm/vec4.hpp"
#include "imgui.h"
#include "iris/image.h"
#include "iris/vulkan.h"
#include "iris/wsi/platform_window.h"

#include "iris/components/renderable.h"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <system_error>
#include <type_traits>

namespace iris {

/*!
\brief Holds all state related to a single renderable window.

The Window state is:
- the wsi::PlatformWindow
- Vulkan handles needed to render to a VkSurfaceKHR
- a number of buffered Frame objects
- the ImGui context

A buffered Frame is:
- Vulkan handles to render a single frame

TODO: document attributes
*/
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

  Image depthStencilImage;
  VkImageView depthStencilImageView;

  Image colorTarget;
  VkImageView colorTargetView;

  Image depthStencilTarget;
  VkImageView depthStencilTargetView;

  /*!
  \brief Holds state that is duplicated for each rendered frame.
  */
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
  glm::vec2 lastMousePos{0.f, 0.f};

  glm::mat4 projectionMatrix{1.f};
  glm::mat4 projectionMatrixInverse{1.f};

  /*!
  \brief Get the current buffered Frame.
  \return the current Frame.
  */
  [[nodiscard]] Frame& currentFrame() noexcept { return frames[frameIndex]; }

  /*!
  \brief Get the previous buffered Frame.
  \return the previous Frame.
  */
  [[nodiscard]] Frame& previousFrame() noexcept {
    return frames[(frameIndex - 1) % frames.size()];
  }

  /*!
  Constructor.
  \param[in] title_ the title of the window.
  \param[in] clearColor_ the color to clear the framebuffer with.
  \param[in] numFrames the number of frames to buffer for rendering.
  */
  Window(std::string title_, VkClearColorValue clearColor_,
         std::size_t numFrames)
    : title(std::move(title_))
    , clearColor(clearColor_)
    , colorImages(numFrames)
    , colorImageViews(numFrames)
    , frames(numFrames)
    , uiContext(nullptr, &ImGui::DestroyContext) {}

  /*!
  \brief No copies.
  */
  Window(Window const&) = delete;

  /*!
  \brief Move constructor.
  */
  Window(Window&&) noexcept;

  /*!
  \brief No copies.
  */
  Window& operator=(Window const&) = delete;

  /*!
  \brief Move assignment: deleted since violates absl::FixedArray invariants.
  */
  Window& operator=(Window&& rhs) noexcept = delete;

  /*!
  \brief Destructor.
  */
  ~Window() noexcept = default;
}; // struct Window

namespace Renderer {

[[nodiscard]] tl::expected<Window, std::exception>
CreateWindow(gsl::czstring<> title, wsi::Offset2D offset, wsi::Extent2D extent,
             glm::vec4 const& clearColor, Window::Options const& options,
             int display, std::uint32_t numFrames) noexcept;

tl::expected<void, std::system_error>
ResizeWindow(Window& window, VkExtent2D newExtent) noexcept;

} // namespace Renderer

/*!
\brief bit-wise or of \ref Window::Options.
*/
inline Window::Options operator|(Window::Options const& lhs,
                                 Window::Options const& rhs) noexcept {
  using U = std::underlying_type_t<Window::Options>;
  return static_cast<Window::Options>(static_cast<U>(lhs) |
                                      static_cast<U>(rhs));
}

/*!
\brief bit-wise or of \ref Window::Options.
*/
inline Window::Options operator|=(Window::Options& lhs,
                                  Window::Options const& rhs) noexcept {
  lhs = lhs | rhs;
  return lhs;
}

/*!
\brief bit-wise and of \ref Window::Options.
*/
inline Window::Options operator&(Window::Options const& lhs,
                                 Window::Options const& rhs) noexcept {
  using U = std::underlying_type_t<Window::Options>;
  return static_cast<Window::Options>(static_cast<U>(lhs) &
                                      static_cast<U>(rhs));
}

/*!
\brief bit-wise and of \ref Window::Options.
*/
inline Window::Options operator&=(Window::Options& lhs,
                                  Window::Options const& rhs) noexcept {
  lhs = lhs & rhs;
  return lhs;
}

} // namespace iris

#endif // HEV_IRIS_WINDOW_H_
