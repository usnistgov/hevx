#include "renderer/surface.h"
#include "absl/container/fixed_array.h"
#include "glm/common.hpp"
#include "logging.h"
#include "renderer/impl.h"
#if defined(VK_USE_PLATFORM_XLIB_KHR)
#include "wsi/window_x11.h"
#elif defined(VK_USE_PLATFORM_WIN32_KHR)
#include "wsi/window_win32.h"
#endif
#include <algorithm>

namespace iris::Renderer {

tl::expected<VkSurfaceKHR, std::error_code>
static CreateSurface(wsi::Window& window) noexcept {
  IRIS_LOG_ENTER();
  VkSurfaceKHR surface;
  auto native = window.NativeHandle();

#if defined(VK_USE_PLATFORM_XLIB_KHR)

  VkXlibSurfaceCreateInfoKHR sci = {};
  sci.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
  sci.dpy = native.display;
  sci.window = native.window;

  VkResult result = vkCreateXlibSurfaceKHR(sInstance, &sci, nullptr, &surface);
  if (result != VK_SUCCESS) {
    GetLogger()->error("Cannot create surface: {}", to_string(result));
    IRIS_LOG_LEAVE();
    return tl::unexpected(Error::kSurfaceCreationFailed);
  }

#elif defined(VK_USE_PLATFORM_WIN32_KHR)

  VkWin32SurfaceCreateInfoKHR sci = {};
  sci.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
  sci.hinstance = native.hInstance;
  sci.hwnd = native.hWnd;

  VkResult result = vkCreateWin32SurfaceKHR(sInstance, &sci, nullptr, &surface);
  if (result != VK_SUCCESS) {
    GetLogger()->error("Cannot create surface: {}", to_string(result));
    IRIS_LOG_LEAVE();
    return tl::unexpected(make_error_code(result));
  }

#endif

  IRIS_LOG_LEAVE();
  return surface;
} // CreateSurface

tl::expected<bool, std::error_code>
static CheckSurfaceSupport(VkSurfaceKHR surface) noexcept {
  IRIS_LOG_ENTER();
  VkBool32 support;
  VkResult result = vkGetPhysicalDeviceSurfaceSupportKHR(
    sPhysicalDevice, sGraphicsQueueFamilyIndex, surface, &support);
  if (result != VK_SUCCESS) {
    GetLogger()->error("Cannot check for physical device surface support: {}",
                        to_string(result));
    IRIS_LOG_LEAVE();
    return tl::unexpected(make_error_code(result));
  }

  IRIS_LOG_LEAVE();
  return (support == VK_TRUE);
} // CheckSurfaceSupport

tl::expected<bool, std::error_code> static CheckSurfaceFormat(
  VkSurfaceKHR surface, VkSurfaceFormatKHR desired) noexcept {
  IRIS_LOG_ENTER();
  VkResult result;

  std::uint32_t numSurfaceFormats;
  result = vkGetPhysicalDeviceSurfaceFormatsKHR(sPhysicalDevice, surface,
                                                &numSurfaceFormats, nullptr);
  if (result != VK_SUCCESS) {
    GetLogger()->error("Cannot get physical device surface formats: {}",
                        to_string(result));
    IRIS_LOG_LEAVE();
    return tl::unexpected(make_error_code(result));
  }

  absl::FixedArray<VkSurfaceFormatKHR> surfaceFormats(numSurfaceFormats);
  result = vkGetPhysicalDeviceSurfaceFormatsKHR(
    sPhysicalDevice, surface, &numSurfaceFormats, surfaceFormats.data());
  if (result != VK_SUCCESS) {
    GetLogger()->error("Cannot get physical device surface formats: {}",
                        to_string(result));
    IRIS_LOG_LEAVE();
    return tl::unexpected(make_error_code(result));
  }

  if (numSurfaceFormats == 1 &&
      surfaceFormats[0].format == VK_FORMAT_UNDEFINED) {
    IRIS_LOG_LEAVE();
    return true;
  }

  for (auto&& supported : surfaceFormats) {
    if (supported.format == desired.format &&
        supported.colorSpace == desired.colorSpace) {
      IRIS_LOG_LEAVE();
      return true;
    }
  }

  IRIS_LOG_LEAVE();
  return false;
} // CheckSurfaceFormat

} // namespace iris::Renderer

tl::expected<iris::Renderer::Surface, std::error_code>
iris::Renderer::Surface::Create(wsi::Window& window,
                                glm::vec4 const& clearColor) noexcept {
  IRIS_LOG_ENTER();

  Surface surface;
  surface.clearColor.float32[0] = clearColor[0];
  surface.clearColor.float32[1] = clearColor[1];
  surface.clearColor.float32[2] = clearColor[2];
  surface.clearColor.float32[3] = clearColor[3];

  if (auto sfc = CreateSurface(window)) {
    surface.handle = *sfc;
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(sfc.error());
  }

  if (auto chk = CheckSurfaceSupport(surface.handle)) {
    if (!*chk) {
      GetLogger()->error("Surface is not supported by the physical device.");
      IRIS_LOG_LEAVE();
      return tl::unexpected(Error::kSurfaceNotSupported);
    }
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(chk.error());
  }

  if (auto chk = CheckSurfaceFormat(surface.handle, sSurfaceColorFormat)) {
    if (!*chk) {
      GetLogger()->error("Surface format is not supported.");
      IRIS_LOG_LEAVE();
      return tl::unexpected(Error::kSurfaceNotSupported);
    }
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(chk.error());
  }

  VkSemaphoreCreateInfo sci = {};
  sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  if (auto result =
        vkCreateSemaphore(sDevice, &sci, nullptr, &surface.imageAvailable);
      result != VK_SUCCESS) {
    GetLogger()->error("Cannot create semaphore: {}", to_string(result));
    IRIS_LOG_LEAVE();
    return tl::unexpected(make_error_code(result));
  }

  if (auto error = surface.Resize(window.Extent())) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(error);
  }

  IRIS_LOG_LEAVE();
  return std::move(surface);
} // iris::Renderer::Surface::Create

namespace iris::Renderer {

static tl::expected<VkSwapchainKHR, std::error_code>
CreateSwapchain(VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR caps,
                VkExtent2D extent, VkSwapchainKHR oldSwapchain) {
  IRIS_LOG_ENTER();
  VkResult result;

  VkSwapchainCreateInfoKHR sci = {};
  sci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  sci.surface = surface;
  sci.minImageCount = caps.minImageCount;
  sci.imageFormat = sSurfaceColorFormat.format;
  sci.imageColorSpace = sSurfaceColorFormat.colorSpace;
  sci.imageExtent = extent;
  sci.imageArrayLayers = 1;
  sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  sci.queueFamilyIndexCount = 0;
  sci.pQueueFamilyIndices = nullptr;
  sci.preTransform = caps.currentTransform;
  sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  sci.presentMode = sSurfacePresentMode;
  sci.clipped = VK_TRUE;
  sci.oldSwapchain = oldSwapchain;

  VkSwapchainKHR newSwapchain;
  result = vkCreateSwapchainKHR(sDevice, &sci, nullptr, &newSwapchain);
  if (result != VK_SUCCESS) {
    GetLogger()->error("Cannot create swapchain: {}", to_string(result));
    IRIS_LOG_LEAVE();
    return tl::unexpected(make_error_code(result));
  }

  IRIS_LOG_LEAVE();
  return newSwapchain;
} // CreateSwapchain

static tl::expected<VkFramebuffer, std::error_code>
CreateFramebuffer(gsl::span<VkImageView> attachments,
                  VkExtent2D extent) noexcept {
  IRIS_LOG_ENTER();
  VkResult result;

  VkFramebufferCreateInfo ci = {};
  ci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
  ci.renderPass = sRenderPass;
  ci.attachmentCount = gsl::narrow_cast<std::uint32_t>(attachments.size());
  ci.pAttachments = attachments.data();
  ci.width = extent.width;
  ci.height = extent.height;
  ci.layers = 1;

  VkFramebuffer framebuffer;
  result = vkCreateFramebuffer(sDevice, &ci, nullptr, &framebuffer);
  if (result != VK_SUCCESS) {
    GetLogger()->error("Cannot create framebuffer: {}", to_string(result));
    IRIS_LOG_LEAVE();
    return tl::unexpected(make_error_code(result));
  }

  IRIS_LOG_LEAVE();
  return framebuffer;
} // CreateFramebuffer

} // namespace iris::Renderer

std::error_code
iris::Renderer::Surface::Resize(glm::uvec2 const& newSize) noexcept {
  IRIS_LOG_ENTER();
  VkResult result;
  GetLogger()->debug("Surface resizing to ({}x{})", newSize[0], newSize[1]);

  VkSurfaceCapabilities2KHR surfaceCapabilities = {};
  surfaceCapabilities.sType = VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR;

  VkPhysicalDeviceSurfaceInfo2KHR surfaceInfo = {};
  surfaceInfo.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SURFACE_INFO_2_KHR;
  surfaceInfo.surface = handle;

  result = vkGetPhysicalDeviceSurfaceCapabilities2KHR(
    sPhysicalDevice, &surfaceInfo, &surfaceCapabilities);
  if (result != VK_SUCCESS) {
    GetLogger()->error("Cannot query for surface capabilities: {}",
                        to_string(result));
    IRIS_LOG_LEAVE();
    return make_error_code(result);
  } 

  VkSurfaceCapabilitiesKHR caps = surfaceCapabilities.surfaceCapabilities;

  VkExtent2D newExtent = {
    caps.currentExtent.width == UINT32_MAX
      ? glm::clamp(newSize[0], caps.minImageExtent.width,
                   caps.maxImageExtent.width)
      : caps.currentExtent.width,
    caps.currentExtent.height == UINT32_MAX
      ? glm::clamp(newSize[1], caps.minImageExtent.height,
                   caps.maxImageExtent.height)
      : caps.currentExtent.height,
  };

  VkExtent3D imageExtent{newExtent.width, newExtent.height, 1};

  VkViewport newViewport{0.f,
                         0.f,
                         static_cast<float>(newExtent.width),
                         static_cast<float>(newExtent.height),
                         0.f,
                         1.f};

  VkRect2D newScissor{{0, 0}, newExtent};

  VkSwapchainKHR newSwapchain{VK_NULL_HANDLE};
  if (auto swpc = CreateSwapchain(handle, caps, newExtent, swapchain)) {
    newSwapchain = *swpc;
  } else {
    IRIS_LOG_LEAVE();
    return swpc.error();
  }

  uint32_t numSwapchainImages;
  result = vkGetSwapchainImagesKHR(sDevice, newSwapchain, &numSwapchainImages,
                                   nullptr);
  if (result != VK_SUCCESS) {
    GetLogger()->error("Cannot get swapchain images: {}", to_string(result));
    vkDestroySwapchainKHR(sDevice, newSwapchain, nullptr);
    IRIS_LOG_LEAVE();
    return make_error_code(result);
  }

  std::vector<VkImage> newColorImages(numSwapchainImages);
  result = vkGetSwapchainImagesKHR(sDevice, newSwapchain, &numSwapchainImages,
                                   newColorImages.data());
  if (result != VK_SUCCESS) {
    GetLogger()->error("Cannot get swapchain images: {}", to_string(result));
    vkDestroySwapchainKHR(sDevice, newSwapchain, nullptr);
    IRIS_LOG_LEAVE();
    return make_error_code(result);
  }

  std::vector<VkImageView> newColorImageViews(numSwapchainImages);
  for (std::uint32_t i = 0; i < numSwapchainImages; ++i) {
    if (auto view = CreateImageView(
          newColorImages[i], sSurfaceColorFormat.format, VK_IMAGE_VIEW_TYPE_2D,
          {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1})) {
      newColorImageViews[i] = *view;
    } else {
      vkDestroySwapchainKHR(sDevice, newSwapchain, nullptr);
      IRIS_LOG_LEAVE();
      return view.error();
    }
  }

  VkImage newDepthStencilImage{VK_NULL_HANDLE};
  VmaAllocation newDepthStencilImageAllocation{VK_NULL_HANDLE};
  VkImageView newDepthStencilImageView{VK_NULL_HANDLE};

  VkImage newColorTarget{VK_NULL_HANDLE};
  VmaAllocation newColorTargetAllocation{VK_NULL_HANDLE};
  VkImageView newColorTargetView{VK_NULL_HANDLE};

  VkImage newDepthStencilTarget{VK_NULL_HANDLE};
  VmaAllocation newDepthStencilTargetAllocation{VK_NULL_HANDLE};
  VkImageView newDepthStencilTargetView{VK_NULL_HANDLE};

  absl::FixedArray<VkImageView> attachments(sNumRenderPassAttachments);
  std::vector<VkFramebuffer> newFramebuffers(numSwapchainImages);

  std::error_code error;

  if (auto dsi = CreateImageAndView(
        VK_IMAGE_TYPE_2D, sSurfaceDepthStencilFormat, imageExtent, 1, 1,
        VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY, VK_IMAGE_VIEW_TYPE_2D,
        {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1})) {
    std::tie(newDepthStencilImage, newDepthStencilImageAllocation,
             newDepthStencilImageView) = *dsi;
  } else {
    error = dsi.error();
    goto fail;
  }

  if (auto ctv =
        CreateImageAndView(VK_IMAGE_TYPE_2D, sSurfaceColorFormat.format,
                           imageExtent, 1, 1, sSurfaceSampleCount,
                           VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                             VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
                           VMA_MEMORY_USAGE_GPU_ONLY, VK_IMAGE_VIEW_TYPE_2D,
                           {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1})) {
    std::tie(newColorTarget, newColorTargetAllocation, newColorTargetView) =
      *ctv;
  } else {
    error = ctv.error();
    goto fail;
  }

  GetLogger()->debug("Transitioning new color target");
  if (auto err = TransitionImage(newColorTarget, VK_IMAGE_LAYOUT_UNDEFINED,
                                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)) {
    error = err;
    goto fail;
  }

  if (auto dstv = CreateImageAndView(
        VK_IMAGE_TYPE_2D, sSurfaceDepthStencilFormat, imageExtent, 1, 1,
        sSurfaceSampleCount, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        VMA_MEMORY_USAGE_GPU_ONLY, VK_IMAGE_VIEW_TYPE_2D,
        {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1})) {
    std::tie(newDepthStencilTarget, newDepthStencilTargetAllocation,
             newDepthStencilTargetView) = *dstv;
  } else {
    error = dstv.error();
    goto fail;
  }

  GetLogger()->debug("Transitioning new depth target");
  if (auto err =
        TransitionImage(newDepthStencilTarget, VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)) {
    error = err;
    goto fail;
  }

  attachments[sColorTargetAttachmentIndex] = newColorTargetView;
  attachments[sDepthStencilTargetAttachmentIndex] = newDepthStencilTargetView;
  attachments[sDepthStencilResolveAttachmentIndex] = newDepthStencilImageView;

  for (std::uint32_t i = 0; i < numSwapchainImages; ++i) {
    attachments[sColorResolveAttachmentIndex] = newColorImageViews[i];
    if (auto fb = CreateFramebuffer(attachments, newExtent)) {
      newFramebuffers[i] = *fb;
    } else {
      error = fb.error();
      goto fail;
    }
  }

  if (swapchain != VK_NULL_HANDLE) Release();

  extent = newExtent;
  viewport = newViewport;
  scissor = newScissor;
  swapchain = newSwapchain;

  colorImages = std::move(newColorImages);
  colorImageViews = std::move(newColorImageViews);

  depthStencilImage = newDepthStencilImage;
  depthStencilImageAllocation = newDepthStencilImageAllocation;
  depthStencilImageView = newDepthStencilImageView;

  colorTarget = newColorTarget;
  colorTargetAllocation = newColorTargetAllocation;
  colorTargetView = newColorTargetView;

  depthStencilTarget = newDepthStencilTarget;
  depthStencilTargetAllocation = newDepthStencilTargetAllocation;
  depthStencilTargetView = newDepthStencilTargetView;

  framebuffers = std::move(newFramebuffers);

  IRIS_LOG_LEAVE();
  return VulkanResult::kSuccess;

fail:
  GetLogger()->debug("Surface::Resize cleaning up on on failure");
  for (auto&& framebuffer : newFramebuffers) {
    vkDestroyFramebuffer(sDevice, framebuffer, nullptr);
  }

  if (newColorTargetView != VK_NULL_HANDLE) {
   vkDestroyImageView(sDevice, newColorTargetView, nullptr);
   vmaDestroyImage(sAllocator, newColorTarget, newColorTargetAllocation);
  }

  if (newDepthStencilTarget != VK_NULL_HANDLE) {
    vkDestroyImageView(sDevice, newDepthStencilTargetView, nullptr);
    vmaDestroyImage(sAllocator, newDepthStencilTarget,
                    newDepthStencilTargetAllocation);
  }

  if (newDepthStencilImage != VK_NULL_HANDLE) {
    vkDestroyImageView(sDevice, newDepthStencilImageView, nullptr);
    vmaDestroyImage(sAllocator, newDepthStencilImage,
                    newDepthStencilImageAllocation);
  }

  for (auto&& imageView : newColorImageViews) {
    vkDestroyImageView(sDevice, imageView, nullptr);
  }

  vkDestroySwapchainKHR(sDevice, newSwapchain, nullptr);

  IRIS_LOG_LEAVE();
  return error;
} // iris::Renderer::Surface::Resize

iris::Renderer::Surface::Surface(Surface&& other) noexcept
  : handle{other.handle}
  , imageAvailable{other.imageAvailable}
  , extent{other.extent}
  , viewport{other.viewport}
  , scissor{other.scissor}
  , clearColor{other.clearColor}
  , swapchain{other.swapchain}
  , colorImages{std::move(other.colorImages)}
  , colorImageViews{std::move(other.colorImageViews)}
  , depthStencilImage{other.depthStencilImage}
  , depthStencilImageAllocation{other.depthStencilImageAllocation}
  , depthStencilImageView{other.depthStencilImageView}
  , colorTarget{other.colorTarget}
  , colorTargetAllocation{other.colorTargetAllocation}
  , colorTargetView{other.colorTargetView}
  , depthStencilTarget{other.depthStencilTarget}
  , depthStencilTargetAllocation{other.depthStencilTargetAllocation}
  , depthStencilTargetView{other.depthStencilTargetView}
  , framebuffers{std::move(other.framebuffers)} {
  IRIS_LOG_ENTER();

  other.handle = VK_NULL_HANDLE;
  other.handle = VK_NULL_HANDLE;
  other.imageAvailable = VK_NULL_HANDLE;
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

  IRIS_LOG_LEAVE();
} // iris::Renderer::Surface

iris::Renderer::Surface& iris::Renderer::Surface::operator=(Surface&& other) noexcept {
  if (this == &other) return *this;
  IRIS_LOG_ENTER();

  handle = other.handle;
  imageAvailable = other.imageAvailable;
  extent = other.extent;
  viewport = other.viewport;
  scissor = other.scissor;
  clearColor = other.clearColor;
  swapchain = other.swapchain;
  colorImages = std::move(other.colorImages);
  colorImageViews = std::move(other.colorImageViews);
  depthStencilImage = other.depthStencilImage;
  depthStencilImageAllocation = other.depthStencilImageAllocation;
  depthStencilImageView = other.depthStencilImageView;
  colorTarget = other.colorTarget;
  colorTargetAllocation = other.colorTargetAllocation;
  colorTargetView = other.colorTargetView;
  depthStencilTarget = other.depthStencilTarget;
  depthStencilTargetAllocation = other.depthStencilTargetAllocation;
  depthStencilTargetView = other.depthStencilTargetView;
  framebuffers = std::move(other.framebuffers);

  other.handle = VK_NULL_HANDLE;
  other.imageAvailable = VK_NULL_HANDLE;
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

  IRIS_LOG_LEAVE();
  return *this;
} // iris::Renderer::Surface::operator=

iris::Renderer::Surface::~Surface() noexcept {
  if (handle == VK_NULL_HANDLE) return;

  IRIS_LOG_ENTER();
  Release();
  vkDestroySemaphore(sDevice, imageAvailable, nullptr);
  vkDestroySurfaceKHR(sInstance, handle, nullptr);
  IRIS_LOG_LEAVE();
} // iris::Renderer::Surface::~Surface

void iris::Renderer::Surface::Release() noexcept {
  IRIS_LOG_ENTER();
  for (auto&& framebuffer : framebuffers) {
    vkDestroyFramebuffer(sDevice, framebuffer, nullptr);
  }

  vkDestroyImageView(sDevice, depthStencilTargetView, nullptr);
  vmaDestroyImage(sAllocator, depthStencilTarget, depthStencilTargetAllocation);

  vkDestroyImageView(sDevice, colorTargetView, nullptr);
  vmaDestroyImage(sAllocator, colorTarget, colorTargetAllocation);

  vkDestroyImageView(sDevice, depthStencilImageView, nullptr);
  vmaDestroyImage(sAllocator, depthStencilImage, depthStencilImageAllocation);

  for (auto&& imageView : colorImageViews) {
    vkDestroyImageView(sDevice, imageView, nullptr);
  }

  //GetLogger()->debug("Swapchain: {}", static_cast<void*>(swapchain));
  vkDestroySwapchainKHR(sDevice, swapchain, nullptr);
  IRIS_LOG_LEAVE();
} // iris::Renderer::Surface::Release

