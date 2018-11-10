#ifndef HEV_IRIS_RENDERER_SURFACE_H_
#define HEV_IRIS_RENDERER_SURFACE_H_

#include "absl/container/inlined_vector.h"
#include "glm/vec4.hpp"
#include "iris/renderer/image.h"
#include "iris/renderer/impl.h"
#include "iris/wsi/window.h"
#include <string>
#include <system_error>

namespace iris::Renderer {

struct Framebuffer {
  static tl::expected<Framebuffer, std::system_error>
  Create(gsl::span<VkImageView> attachments, VkExtent2D extent,
         std::string name = {}) noexcept;

  VkFramebuffer handle{VK_NULL_HANDLE};

  operator VkFramebuffer() const noexcept { return handle; }

  Framebuffer() = default;
  Framebuffer(Framebuffer const&) = delete;
  Framebuffer(Framebuffer&& other) noexcept;
  Framebuffer& operator=(Framebuffer const&) = delete;
  Framebuffer& operator=(Framebuffer&& other) noexcept;
  ~Framebuffer() noexcept;

private:
  std::string name;
}; // struct Framebuffer

struct Surface {
  static tl::expected<Surface, std::system_error>
  Create(wsi::Window& window, glm::vec4 const& clearColor) noexcept;

  std::system_error Resize(VkExtent2D newExtent) noexcept;

  VkSurfaceKHR handle{VK_NULL_HANDLE};
  VkSemaphore imageAvailable{VK_NULL_HANDLE};

  VkExtent2D extent{};
  VkViewport viewport{};
  VkRect2D scissor{};
  VkClearColorValue clearColor{};

  VkSwapchainKHR swapchain{VK_NULL_HANDLE};

  absl::InlinedVector<VkImage, 4> colorImages{};
  absl::InlinedVector<ImageView, 4> colorImageViews{};

  Image depthStencilImage{};
  ImageView depthStencilImageView{};

  Image colorTarget{};
  ImageView colorTargetView{};

  Image depthStencilTarget{};
  ImageView depthStencilTargetView{};

  absl::InlinedVector<Framebuffer, 4> framebuffers{};

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
}; // struct Surface

} // namespace iris::Renderer

#endif // HEV_IRIS_RENDERER_SURFACE_H_

