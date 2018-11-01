#ifndef HEV_IRIS_RENDERER_SURFACE_H_
#define HEV_IRIS_RENDERER_SURFACE_H_

#include "iris/renderer/vulkan.h"
#include "iris/wsi/window.h"
#include "tl/expected.hpp"
#include "glm/vec4.hpp"
#include <system_error>
#include <vector>

namespace iris::Renderer {

struct Surface {
  static tl::expected<Surface, std::error_code>
  Create(wsi::Window& window, glm::vec4 const& clearColor) noexcept;

  std::error_code Resize(glm::uvec2 const& newExtent) noexcept;

  VkSurfaceKHR handle{VK_NULL_HANDLE};
  VkSemaphore imageAvailable{VK_NULL_HANDLE};

  VkExtent2D extent{};
  VkViewport viewport{};
  VkRect2D scissor{};
  VkClearColorValue clearColor{};

  VkSwapchainKHR swapchain{VK_NULL_HANDLE};

  std::vector<VkImage> colorImages{};
  std::vector<VkImageView> colorImageViews{};

  VkImage depthStencilImage{VK_NULL_HANDLE};
  VmaAllocation depthStencilImageAllocation{VK_NULL_HANDLE};
  VkImageView depthStencilImageView{VK_NULL_HANDLE};

  VkImage colorTarget{VK_NULL_HANDLE};
  VmaAllocation colorTargetAllocation{VK_NULL_HANDLE};
  VkImageView colorTargetView{VK_NULL_HANDLE};

  VkImage depthStencilTarget{VK_NULL_HANDLE};
  VmaAllocation depthStencilTargetAllocation{VK_NULL_HANDLE};
  VkImageView depthStencilTargetView{VK_NULL_HANDLE};

  std::vector<VkFramebuffer> framebuffers{};

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

