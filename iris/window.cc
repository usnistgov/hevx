#include "window.h"

#include "config.h"

#include "enumerate.h"
#include "error.h"
#include "glm/gtc/matrix_transform.hpp"
#include "logging.h"
#include "renderer.h"
#include "renderer_util.h"
#include "wsi/input.h"
#if PLATFORM_LINUX
#include "wsi/platform_window_x11.h"
#elif PLATFORM_WINDOWS
#include "wsi/platform_window_win32.h"
#endif

iris::Window::Window(Window&& other) noexcept
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
  , depthStencilImage(std::move(other.depthStencilImage))
  , depthStencilImageView(other.depthStencilImageView)
  , colorTarget(std::move(other.colorTarget))
  , colorTargetView(other.colorTargetView)
  , depthStencilTarget(std::move(other.depthStencilTarget))
  , depthStencilTargetView(other.depthStencilTargetView)
  , frames(std::move(other.frames))
  , frameIndex(other.frameIndex)
  , imageAcquired(other.imageAcquired)
  , uiContext(std::move(other.uiContext))
  , uiRenderable(std::move(other.uiRenderable))
  , lastMousePos(std::move(other.lastMousePos))
  , projectionMatrix(std::move(other.projectionMatrix))
  , projectionMatrixInverse(std::move(other.projectionMatrixInverse)) {
} // Window::Window

tl::expected<iris::Window, std::exception>
iris::Renderer::CreateWindow(gsl::czstring<> title, wsi::Offset2D offset,
                             wsi::Extent2D extent, glm::vec4 const& clearColor,
                             Window::Options const& options, int display,
                             std::uint32_t numFrames) noexcept {
  using namespace std::string_literals;

  IRIS_LOG_ENTER();
  Expects(sInstance != VK_NULL_HANDLE);
  Expects(sPhysicalDevice != VK_NULL_HANDLE);
  Expects(sDevice != VK_NULL_HANDLE);

  Window window(title, {clearColor.r, clearColor.g, clearColor.b, clearColor.a},
                numFrames);
  window.showUI =
    (options & Window::Options::kShowUI) == Window::Options::kShowUI;

  wsi::PlatformWindow::Options platformOptions =
    wsi::PlatformWindow::Options::kSizeable;
  if ((options & Window::Options::kDecorated) == Window::Options::kDecorated) {
    platformOptions |= wsi::PlatformWindow::Options::kDecorated;
  }

  if (auto win =
        wsi::PlatformWindow::Create(title, std::move(offset), std::move(extent),
                                    platformOptions, display)) {
    window.platformWindow = std::move(*win);
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(win.error());
  }

#if defined(VK_USE_PLATFORM_XCB_KHR)

  VkXcbSurfaceCreateInfoKHR sci = {};
  sci.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
  std::tie(sci.connection, sci.window) = window.platformWindow.NativeHandle();

  if (auto result =
        vkCreateXcbSurfaceKHR(sInstance, &sci, nullptr, &window.surface);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(make_error_code(result), "Cannot create surface"));
  }

#elif defined(VK_USE_PLATFORM_WIN32_KHR)

  VkWin32SurfaceCreateInfoKHR sci = {};
  sci.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
  std::tie(sci.hinstance, sci.hwnd) = window.platformWindow.NativeHandle();

  if (auto result =
        vkCreateWin32SurfaceKHR(sInstance, &sci, nullptr, &window.surface);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(make_error_code(result), "Cannot create surface"));
  }

#endif

  NameObject(VK_OBJECT_TYPE_SURFACE_KHR, window.surface,
             fmt::format("{}.surface", title).c_str());

  VkBool32 surfaceSupported;
  if (auto result = vkGetPhysicalDeviceSurfaceSupportKHR(
        sPhysicalDevice, sQueueFamilyIndex, window.surface, &surfaceSupported);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(make_error_code(result),
                        "Cannot check for physical device surface support"));
  }

  if (surfaceSupported == VK_FALSE) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(Error::kSurfaceNotSupported,
                        "Surface is not supported by physical device."));
  }

  bool formatSupported = false;
  if (auto surfaceFormats =
        GetPhysicalDeviceSurfaceFormats(sPhysicalDevice, window.surface)) {
    if (surfaceFormats->size() == 1 &&
        (*surfaceFormats)[0].format == VK_FORMAT_UNDEFINED) {
      formatSupported = true;
    } else {
      for (auto&& supported : *surfaceFormats) {
        if (supported.format == sSurfaceColorFormat.format &&
            supported.colorSpace == sSurfaceColorFormat.colorSpace) {
          formatSupported = true;
          break;
        }
      }
    }
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(surfaceFormats.error());
  }

  if (!formatSupported) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(Error::kSurfaceNotSupported,
                        "Surface format is not supported by physical device"));
  }

  VkSemaphoreCreateInfo semaphoreCI = {};
  semaphoreCI.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

  VkCommandPoolCreateInfo commandPoolCI = {};
  commandPoolCI.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  commandPoolCI.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  commandPoolCI.queueFamilyIndex = sQueueFamilyIndex;

  VkCommandBufferAllocateInfo commandBufferAI = {};
  commandBufferAI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  commandBufferAI.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  commandBufferAI.commandBufferCount = 1;

  for (auto&& [i, frame] : enumerate(window.frames)) {
    if (auto result = vkCreateSemaphore(sDevice, &semaphoreCI, nullptr,
                                        &frame.imageAvailable);
        result != VK_SUCCESS) {
      IRIS_LOG_LEAVE();
      return tl::unexpected(std::system_error(
        make_error_code(result), "Cannot create image available semaphore"));
    }

    NameObject(VK_OBJECT_TYPE_SEMAPHORE, frame.imageAvailable,
               fmt::format("{}.frames[{}].imageAvailable", title, i).c_str());

    if (auto result = vkCreateCommandPool(sDevice, &commandPoolCI, nullptr,
                                          &frame.commandPool);
        result != VK_SUCCESS) {
      IRIS_LOG_LEAVE();
      return tl::unexpected(std::system_error(make_error_code(result),
                                              "Cannot create command pool"));
    }

    NameObject(VK_OBJECT_TYPE_COMMAND_POOL, frame.commandPool,
               fmt::format("{}.frames[{}].commandPool", title, i).c_str());

    commandBufferAI.commandPool = frame.commandPool;

    if (auto result = vkAllocateCommandBuffers(sDevice, &commandBufferAI,
                                               &frame.commandBuffer);
        result != VK_SUCCESS) {
      IRIS_LOG_LEAVE();
      return tl::unexpected(std::system_error(
        make_error_code(result), "Cannot allocate command buffer"));
    }

    NameObject(VK_OBJECT_TYPE_COMMAND_BUFFER, frame.commandBuffer,
               fmt::format("{}.frames[{}].commandBuffer", title, i).c_str());
  }

  if (auto result = ResizeWindow(window, {extent.width, extent.height});
      !result) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(result.error());
  }

  window.uiContext.reset(ImGui::CreateContext());
  ImGui::SetCurrentContext(window.uiContext.get());
  ImGui::StyleColorsDark();

  ImGuiIO& io = ImGui::GetIO();

  io.BackendRendererName = "hevx::iris";
  io.BackendRendererName = "";

  io.Fonts->AddFontFromFileTTF(
    (kIRISContentDirectory + "/assets/fonts/SourceSansPro-Regular.ttf"s)
      .c_str(),
    16.f);

  unsigned char* pixels;
  int width, height, bytesPerPixel;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height, &bytesPerPixel);

  Image fontTexture;
  VkImageView fontTextureView;

  if (auto img =
        CreateImage(sCommandPools[0], sCommandQueues[0], sCommandFences[0],
                    VK_FORMAT_R8G8B8A8_UNORM,
                    VkExtent2D{gsl::narrow_cast<std::uint32_t>(width),
                               gsl::narrow_cast<std::uint32_t>(height)},
                    VK_IMAGE_USAGE_SAMPLED_BIT, VMA_MEMORY_USAGE_GPU_ONLY,
                    gsl::not_null(reinterpret_cast<std::byte*>(pixels)),
                    gsl::narrow_cast<std::uint32_t>(bytesPerPixel))) {
    fontTexture = std::move(*img);
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(img.error());
  }

  if (auto view = CreateImageView(fontTexture, VK_IMAGE_VIEW_TYPE_2D,
                                  VK_FORMAT_R8G8B8A8_UNORM,
                                  {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1})) {
    fontTextureView = std::move(*view);
  } else {
    IRIS_LOG_LEAVE();
    vmaDestroyImage(sAllocator, fontTexture.image, fontTexture.allocation);
    return tl::unexpected(view.error());
  }

  NameObject(
    VK_OBJECT_TYPE_IMAGE, fontTexture.image,
    fmt::format("{}.uiRenderable.textures[0] (fontTexture)", title).c_str());
  NameObject(
    VK_OBJECT_TYPE_IMAGE_VIEW, fontTextureView,
    fmt::format("{}.uiRenderable.views[0] (fontTextureView)", title).c_str());

  VkSamplerCreateInfo samplerCI = {};
  samplerCI.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerCI.magFilter = VK_FILTER_LINEAR;
  samplerCI.minFilter = VK_FILTER_LINEAR;
  samplerCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  samplerCI.mipLodBias = 0.f;
  samplerCI.anisotropyEnable = VK_FALSE;
  samplerCI.maxAnisotropy = 1;
  samplerCI.compareEnable = VK_FALSE;
  samplerCI.compareOp = VK_COMPARE_OP_ALWAYS;
  samplerCI.minLod = -1000.f;
  samplerCI.maxLod = 1000.f;
  samplerCI.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
  samplerCI.unnormalizedCoordinates = VK_FALSE;

  VkSampler fontTextureSampler;
  if (auto result =
        vkCreateSampler(sDevice, &samplerCI, nullptr, &fontTextureSampler);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    vkDestroyImageView(sDevice, fontTextureView, nullptr);
    DestroyImage(fontTexture);
    return tl::unexpected(
      std::system_error(make_error_code(result), "Cannot create sampler"));
  }

  NameObject(
    VK_OBJECT_TYPE_SAMPLER, fontTextureSampler,
    fmt::format("{}.uiRenderable.samplers[0] (fontTextureSampler)", title)
      .c_str());

  window.uiRenderable.textures.push_back(std::move(fontTexture));
  window.uiRenderable.textureViews.push_back(fontTextureView);
  window.uiRenderable.textureSamplers.push_back(fontTextureSampler);

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

  window.platformWindow.OnResize(
    [&window](wsi::Extent2D const&) { window.resized = true; });
  window.platformWindow.OnClose([]() { Terminate(); });
  window.platformWindow.Show();

  Ensures(window.surface != VK_NULL_HANDLE);
  Ensures(window.swapchain != VK_NULL_HANDLE);
  Ensures(!window.colorImages.empty());
  Ensures(!window.colorImageViews.empty());
  Ensures(window.depthStencilImage.image != VK_NULL_HANDLE);
  Ensures(window.depthStencilImage.allocation != VK_NULL_HANDLE);
  Ensures(window.depthStencilImageView != VK_NULL_HANDLE);
  Ensures(window.colorTarget.image != VK_NULL_HANDLE);
  Ensures(window.colorTarget.allocation != VK_NULL_HANDLE);
  Ensures(window.colorTargetView != VK_NULL_HANDLE);
  Ensures(window.depthStencilTarget.image != VK_NULL_HANDLE);
  Ensures(window.depthStencilTarget.allocation != VK_NULL_HANDLE);
  Ensures(window.depthStencilTargetView != VK_NULL_HANDLE);
  Ensures(!window.frames.empty());

  IRIS_LOG_LEAVE();
  return std::move(window);
} // iris::Renderer::CreateWindow

tl::expected<void, std::system_error>
iris::Renderer::ResizeWindow(Window& window, VkExtent2D newExtent) noexcept {
  IRIS_LOG_ENTER();
  Expects(sPhysicalDevice != VK_NULL_HANDLE);
  Expects(sDevice != VK_NULL_HANDLE);

  GetLogger()->debug("Resizing window to ({}x{})", newExtent.width,
                     newExtent.height);

  VkSurfaceCapabilities2KHR surfaceCapabilities = {};
  surfaceCapabilities.sType = VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR;

  VkPhysicalDeviceSurfaceInfo2KHR surfaceInfo = {};
  surfaceInfo.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SURFACE_INFO_2_KHR;
  surfaceInfo.surface = window.surface;

  if (auto result = vkGetPhysicalDeviceSurfaceCapabilities2KHR(
        sPhysicalDevice, &surfaceInfo, &surfaceCapabilities);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(make_error_code(result),
                        "Cannot get physical device surface capabilities"));
  }

  VkSurfaceCapabilitiesKHR caps = surfaceCapabilities.surfaceCapabilities;

  newExtent.width = caps.currentExtent.width == UINT32_MAX
                      ? glm::clamp(newExtent.width, caps.minImageExtent.width,
                                   caps.maxImageExtent.width)
                      : caps.currentExtent.width;

  newExtent.height =
    caps.currentExtent.height == UINT32_MAX
      ? glm::clamp(newExtent.height, caps.minImageExtent.height,
                   caps.maxImageExtent.height)
      : caps.currentExtent.height;

  VkViewport newViewport{
    0.f,                                  // x
    0.f,                                  // y
    static_cast<float>(newExtent.width),  // width
    static_cast<float>(newExtent.height), // height
    0.f,                                  // minDepth
    1.f,                                  // maxDepth
  };

  VkRect2D newScissor{{0, 0}, newExtent};

  VkSwapchainCreateInfoKHR swapchainCI = {};
  swapchainCI.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  swapchainCI.surface = window.surface;
  swapchainCI.minImageCount = caps.minImageCount;
  swapchainCI.imageFormat = sSurfaceColorFormat.format;
  swapchainCI.imageColorSpace = sSurfaceColorFormat.colorSpace;
  swapchainCI.imageExtent = newExtent;
  swapchainCI.imageArrayLayers = 1;
  swapchainCI.imageUsage =
    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  swapchainCI.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  swapchainCI.queueFamilyIndexCount = 0;
  swapchainCI.pQueueFamilyIndices = nullptr;
  swapchainCI.preTransform = caps.currentTransform;
  swapchainCI.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  swapchainCI.presentMode = sSurfacePresentMode;
  swapchainCI.clipped = VK_TRUE;
  swapchainCI.oldSwapchain = window.swapchain;

  VkSwapchainKHR newSwapchain;
  if (auto result =
        vkCreateSwapchainKHR(sDevice, &swapchainCI, nullptr, &newSwapchain);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(make_error_code(result), "Cannot create swapchain"));
  }

  std::uint32_t numSwapchainImages;
  if (auto result = vkGetSwapchainImagesKHR(sDevice, newSwapchain,
                                            &numSwapchainImages, nullptr);
      result != VK_SUCCESS) {
    vkDestroySwapchainKHR(sDevice, newSwapchain, nullptr);
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(make_error_code(result),
                                            "Cannot get swapchain images"));
  }

  if (numSwapchainImages != window.colorImages.size()) {
    vkDestroySwapchainKHR(sDevice, newSwapchain, nullptr);
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(
      Error::kWindowResizeFailed,
      "New number of swapchain images not equal to old number"));
  }

  if (numSwapchainImages != window.frames.size()) {
    vkDestroySwapchainKHR(sDevice, newSwapchain, nullptr);
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(
      Error::kWindowResizeFailed,
      "New number of swapchain images not equal to number of frames"));
  }

  absl::FixedArray<VkImage> newColorImages(numSwapchainImages);
  if (auto result = vkGetSwapchainImagesKHR(
        sDevice, newSwapchain, &numSwapchainImages, newColorImages.data());
      result != VK_SUCCESS) {
    vkDestroySwapchainKHR(sDevice, newSwapchain, nullptr);
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(make_error_code(result),
                                            "Cannot get swapchain images"));
  }

  VkImageViewCreateInfo imageViewCI = {};
  imageViewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  imageViewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
  imageViewCI.format = sSurfaceColorFormat.format;
  imageViewCI.components = {
    VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
    VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
  imageViewCI.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

  absl::FixedArray<VkImageView> newColorImageViews(numSwapchainImages);
  for (auto&& [i, view] : enumerate(newColorImageViews)) {
    imageViewCI.image = newColorImages[i];
    if (auto result = vkCreateImageView(sDevice, &imageViewCI, nullptr, &view);
        result != VK_SUCCESS) {
      vkDestroySwapchainKHR(sDevice, newSwapchain, nullptr);
      IRIS_LOG_LEAVE();
      return tl::unexpected(std::system_error(
        make_error_code(result), "Cannot get swapchain image view"));
    }
  }

  Image newDepthStencilImage;
  VkImageView newDepthStencilImageView;

  if (auto img = AllocateImage(
        sSurfaceDepthStencilFormat, newExtent, 1, 1, VK_SAMPLE_COUNT_1_BIT,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_TILING_OPTIMAL,
        VMA_MEMORY_USAGE_GPU_ONLY)) {
    newDepthStencilImage = std::move(*img);
  } else {
    for (auto&& v : newColorImageViews) vkDestroyImageView(sDevice, v, nullptr);
    vkDestroySwapchainKHR(sDevice, newSwapchain, nullptr);
    IRIS_LOG_LEAVE();
    return tl::unexpected(img.error());
  }

  if (auto view = CreateImageView(newDepthStencilImage, VK_IMAGE_VIEW_TYPE_2D,
                                  sSurfaceDepthStencilFormat,
                                  {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1})) {
    newDepthStencilImageView = std::move(*view);
  } else {
    for (auto&& v : newColorImageViews) vkDestroyImageView(sDevice, v, nullptr);
    vkDestroySwapchainKHR(sDevice, newSwapchain, nullptr);
    IRIS_LOG_LEAVE();
    return tl::unexpected(view.error());
  }

  Image newColorTarget;
  VkImageView newColorTargetView;

  if (auto img = AllocateImage(
        sSurfaceColorFormat.format, newExtent, 1, 1, sSurfaceSampleCount,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
          VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
        VK_IMAGE_TILING_OPTIMAL, VMA_MEMORY_USAGE_GPU_ONLY)) {
    newColorTarget = std::move(*img);
  } else {
    vkDestroyImageView(sDevice, newDepthStencilImageView, nullptr);
    DestroyImage(newDepthStencilImage);
    for (auto&& v : newColorImageViews) vkDestroyImageView(sDevice, v, nullptr);
    vkDestroySwapchainKHR(sDevice, newSwapchain, nullptr);
    IRIS_LOG_LEAVE();
    return tl::unexpected(img.error());
  }

  if (auto view = CreateImageView(newColorTarget, VK_IMAGE_VIEW_TYPE_2D,
                                  sSurfaceColorFormat.format,
                                  {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1})) {
    newColorTargetView = std::move(*view);
  } else {
    DestroyImage(newColorTarget);
    vkDestroyImageView(sDevice, newDepthStencilImageView, nullptr);
    DestroyImage(newDepthStencilImage);
    for (auto&& v : newColorImageViews) vkDestroyImageView(sDevice, v, nullptr);
    vkDestroySwapchainKHR(sDevice, newSwapchain, nullptr);
    IRIS_LOG_LEAVE();
    return tl::unexpected(view.error());
  }

  Image newDepthStencilTarget;
  VkImageView newDepthStencilTargetView;

  if (auto img = AllocateImage(
        sSurfaceDepthStencilFormat, newExtent, 1, 1, sSurfaceSampleCount,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_TILING_OPTIMAL,
        VMA_MEMORY_USAGE_GPU_ONLY)) {
    newDepthStencilTarget = std::move(*img);
  } else {
    vkDestroyImageView(sDevice, newColorTargetView, nullptr);
    DestroyImage(newColorTarget);
    vkDestroyImageView(sDevice, newDepthStencilImageView, nullptr);
    DestroyImage(newDepthStencilImage);
    for (auto&& v : newColorImageViews) vkDestroyImageView(sDevice, v, nullptr);
    vkDestroySwapchainKHR(sDevice, newSwapchain, nullptr);
    IRIS_LOG_LEAVE();
    return tl::unexpected(img.error());
  }

  if (auto view = CreateImageView(newDepthStencilTarget, VK_IMAGE_VIEW_TYPE_2D,
                                  sSurfaceDepthStencilFormat,
                                  {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1})) {
    newDepthStencilTargetView = std::move(*view);
  } else {
    DestroyImage(newDepthStencilTarget);
    vkDestroyImageView(sDevice, newColorTargetView, nullptr);
    DestroyImage(newColorTarget);
    vkDestroyImageView(sDevice, newDepthStencilImageView, nullptr);
    DestroyImage(newDepthStencilImage);
    for (auto&& v : newColorImageViews) vkDestroyImageView(sDevice, v, nullptr);
    vkDestroySwapchainKHR(sDevice, newSwapchain, nullptr);
    IRIS_LOG_LEAVE();
    return tl::unexpected(view.error());
  }

  if (auto result =
        TransitionImage(sCommandPools[0], sCommandQueues[0], sCommandFences[0],
                        newColorTarget.image, VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 1, 1);
      !result) {
    DestroyImage(newDepthStencilTarget);
    vkDestroyImageView(sDevice, newDepthStencilTargetView, nullptr);
    DestroyImage(newColorTarget);
    vkDestroyImageView(sDevice, newColorTargetView, nullptr);
    DestroyImage(newDepthStencilTarget);
    vkDestroyImageView(sDevice, newDepthStencilImageView, nullptr);
    for (auto&& v : newColorImageViews) vkDestroyImageView(sDevice, v, nullptr);
    vkDestroySwapchainKHR(sDevice, newSwapchain, nullptr);
    IRIS_LOG_LEAVE();
    return tl::unexpected(result.error());
  }

  if (auto result =
        TransitionImage(sCommandPools[0], sCommandQueues[0], sCommandFences[0],
                        newDepthStencilTarget.image, VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, 1, 1);
      !result) {
    DestroyImage(newDepthStencilTarget);
    vkDestroyImageView(sDevice, newDepthStencilTargetView, nullptr);
    DestroyImage(newColorTarget);
    vkDestroyImageView(sDevice, newColorTargetView, nullptr);
    DestroyImage(newDepthStencilTarget);
    vkDestroyImageView(sDevice, newDepthStencilImageView, nullptr);
    for (auto&& v : newColorImageViews) vkDestroyImageView(sDevice, v, nullptr);
    vkDestroySwapchainKHR(sDevice, newSwapchain, nullptr);
    IRIS_LOG_LEAVE();
    return tl::unexpected(result.error());
  }

  absl::FixedArray<VkImageView> attachments(sNumRenderPassAttachments);
  attachments[sColorTargetAttachmentIndex] = newColorTargetView;
  attachments[sDepthStencilTargetAttachmentIndex] = newDepthStencilTargetView;
  attachments[sDepthStencilResolveAttachmentIndex] = newDepthStencilImageView;

  VkFramebufferCreateInfo framebufferCI = {};
  framebufferCI.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
  framebufferCI.renderPass = sRenderPass;
  framebufferCI.attachmentCount = attachments.size();
  framebufferCI.width = newExtent.width;
  framebufferCI.height = newExtent.height;
  framebufferCI.layers = 1;

  absl::FixedArray<VkFramebuffer> newFramebuffers(numSwapchainImages);

  for (auto&& [i, framebuffer] : enumerate(newFramebuffers)) {
    attachments[sColorResolveAttachmentIndex] = newColorImageViews[i];
    framebufferCI.pAttachments = attachments.data();

    if (auto result =
          vkCreateFramebuffer(sDevice, &framebufferCI, nullptr, &framebuffer);
        result != VK_SUCCESS) {
      DestroyImage(newDepthStencilTarget);
      vkDestroyImageView(sDevice, newDepthStencilTargetView, nullptr);
      DestroyImage(newColorTarget);
      vkDestroyImageView(sDevice, newColorTargetView, nullptr);
      DestroyImage(newDepthStencilTarget);
      vkDestroyImageView(sDevice, newDepthStencilImageView, nullptr);
      for (auto&& v : newColorImageViews)
        vkDestroyImageView(sDevice, v, nullptr);
      vkDestroySwapchainKHR(sDevice, newSwapchain, nullptr);
      IRIS_LOG_LEAVE();
      return tl::unexpected(std::system_error(make_error_code(result),
                                              "Cannot create framebuffer"));
    }
  }

  glm::mat4 const newProjectionMatrix =
    glm::perspectiveFov(60.f, static_cast<float>(newExtent.width),
                        static_cast<float>(newExtent.height), .001f, 1000.f);

  if (window.swapchain != VK_NULL_HANDLE) {
    GetLogger()->trace("ResizeWindow: releasing old resources");
    for (auto&& frame : window.frames) {
      vkDestroyFramebuffer(sDevice, frame.framebuffer, nullptr);
    }
    DestroyImage(newDepthStencilTarget);
    vkDestroyImageView(sDevice, newDepthStencilTargetView, nullptr);
    DestroyImage(newColorTarget);
    vkDestroyImageView(sDevice, newColorTargetView, nullptr);
    DestroyImage(newDepthStencilTarget);
    vkDestroyImageView(sDevice, newDepthStencilImageView, nullptr);
    for (auto&& view : window.colorImageViews) {
      vkDestroyImageView(sDevice, view, nullptr);
    }
    vkDestroySwapchainKHR(sDevice, window.swapchain, nullptr);
  }

  window.extent = newExtent;
  window.viewport = newViewport;
  window.scissor = newScissor;

  window.swapchain = newSwapchain;
  NameObject(VK_OBJECT_TYPE_SWAPCHAIN_KHR, window.swapchain,
             fmt::format("{}.swapchain", window.title).c_str());

  std::copy_n(newColorImages.begin(), numSwapchainImages,
              window.colorImages.begin());
  for (auto&& [i, image] : enumerate(window.colorImages)) {
    NameObject(VK_OBJECT_TYPE_IMAGE, image,
               fmt::format("{}.colorImages[{}]", window.title, i).c_str());
  }

  std::copy_n(newColorImageViews.begin(), numSwapchainImages,
              window.colorImageViews.begin());
  for (auto&& [i, view] : enumerate(window.colorImageViews)) {
    NameObject(VK_OBJECT_TYPE_IMAGE_VIEW, view,
               fmt::format("{}.colorImageViews[{}]", window.title, i).c_str());
  }

  window.depthStencilImage = std::move(newDepthStencilImage);
  window.depthStencilImageView = newDepthStencilImageView;
  NameObject(VK_OBJECT_TYPE_IMAGE, window.depthStencilImage.image,
             fmt::format("{}.depthStencilImage", window.title).c_str());
  NameObject(VK_OBJECT_TYPE_IMAGE_VIEW, window.depthStencilImageView,
             fmt::format("{}.depthStencilImageView", window.title).c_str());

  window.colorTarget = std::move(newColorTarget);
  window.colorTargetView = newColorTargetView;
  NameObject(VK_OBJECT_TYPE_IMAGE, window.colorTarget.image,
             fmt::format("{}.colorTarget", window.title).c_str());
  NameObject(VK_OBJECT_TYPE_IMAGE_VIEW, window.colorTargetView,
             fmt::format("{}.colorTargetView", window.title).c_str());

  window.depthStencilTarget = std::move(newDepthStencilTarget);
  window.depthStencilTargetView = newDepthStencilTargetView;
  NameObject(VK_OBJECT_TYPE_IMAGE, window.depthStencilTarget.image,
             fmt::format("{}.depthStencilTarget", window.title).c_str());
  NameObject(VK_OBJECT_TYPE_IMAGE_VIEW, window.depthStencilTargetView,
             fmt::format("{}.depthStencilTargetView", window.title).c_str());

  for (auto&& [i, frame] : enumerate(window.frames)) {
    frame.framebuffer = newFramebuffers[i];
    NameObject(
      VK_OBJECT_TYPE_FRAMEBUFFER, frame.framebuffer,
      fmt::format("{}.frames[{}].framebuffer", window.title, i).c_str());
  }

  window.projectionMatrix = newProjectionMatrix;
  window.projectionMatrixInverse = glm::inverse(window.projectionMatrix);

  IRIS_LOG_LEAVE();
  return {};
} // iris::Renderer::ResizeWindow

