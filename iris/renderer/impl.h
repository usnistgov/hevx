#ifndef HEV_IRIS_RENDERER_IMPL_H_
#define HEV_IRIS_RENDERER_IMPL_H_

#include "config.h"
#include "error.h"
#include "renderer/renderer.h"
#include "renderer/vulkan.h"
#include <cstdint>

namespace iris::Renderer {

extern VkInstance sInstance;
extern VkDebugReportCallbackEXT sDebugReportCallback;
extern VkPhysicalDevice sPhysicalDevice;
extern std::uint32_t sGraphicsQueueFamilyIndex;
extern VkDevice sDevice;
extern VkQueue sUnorderedCommandQueue;
extern VkCommandPool sUnorderedCommandPool;
extern VkFence sUnorderedCommandFence;
extern VmaAllocator sAllocator;

extern VkSurfaceFormatKHR sSurfaceColorFormat;
extern VkFormat sSurfaceDepthFormat;
extern VkSampleCountFlagBits sSurfaceSampleCount;
extern VkPresentModeKHR sSurfacePresentMode;

extern std::uint32_t sNumRenderPassAttachments;
extern VkRenderPass sRenderPass;

} // namespace iris::Renderer

#endif // HEV_IRIS_RENDERER_IMPL_H_

