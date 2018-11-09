#ifndef HEV_IRIS_RENDERER_SURFACE_H_
#define HEV_IRIS_RENDERER_SURFACE_H_

#include "glm/vec4.hpp"
#include "iris/renderer/image.h"
#include "iris/renderer/vulkan.h"
#include "iris/wsi/window.h"
#include "tl/expected.hpp"
#include <system_error>
#include <vector>

namespace iris::Renderer {

struct Surface {
  static tl::expected<Surface, std::system_error>
  Create(wsi::Window& window, glm::vec4 const& clearColor) noexcept;

  tl::expected<void, std::system_error> Resize(VkExtent2D newExtent) noexcept;

  VkSurfaceKHR handle{VK_NULL_HANDLE};
  VkSemaphore imageAvailable{VK_NULL_HANDLE};

  VkExtent2D extent{};
  VkViewport viewport{};
  VkRect2D scissor{};
  VkClearColorValue clearColor{};

  VkSwapchainKHR swapchain{VK_NULL_HANDLE};

  std::vector<VkImage> colorImages{};
  std::vector<VkImageView> colorImageViews{};

  Image depthStencilImage{};
  ImageView depthStencilImageView{};

  Image colorTarget{};
  ImageView colorTargetView{};

  Image depthStencilTarget{};
  ImageView depthStencilTargetView{};

  std::vector<VkFramebuffer> framebuffers{};

  std::uint32_t currentImageIndex{UINT32_MAX};

  inline VkFramebuffer currentFramebuffer() const noexcept {
    return framebuffers[currentImageIndex];
  }

  Surface() = default;
  Surface(Surface const&) = delete;
  Surface(Surface&&) noexcept;
  Surface& operator=(Surface const&) = delete;
  Surface& operator=(Surface&&) noexcept;
  ~Surface() noexcept;

private:
  void Release() noexcept;
}; // struct Surface

} // namespace iris::Renderer

#endif // HEV_IRIS_RENDERER_SURFACE_H_

