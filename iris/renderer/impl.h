#ifndef HEV_IRIS_RENDERER_IMPL_H_
#define HEV_IRIS_RENDERER_IMPL_H_

#include "config.h"
#include "renderer/error.h"
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

} // namespace iris::Renderer

#endif // HEV_IRIS_RENDERER_IMPL_H_
