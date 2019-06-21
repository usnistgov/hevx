#ifndef HEV_IRIS_RENDERER_UTIL_H_
#define HEV_IRIS_RENDERER_UTIL_H_

#include "iris/config.h"

#include "iris/vulkan.h"

#if PLATFORM_COMPILER_MSVC
#include <codeanalysis/warnings.h>
#pragma warning(push)
#pragma warning(disable: ALL_CODE_ANALYSIS_WARNINGS)
#pragma warning(disable: ALL_CPPCORECHECK_WARNINGS)
#endif

#include "absl/container/inlined_vector.h"
#include "glm/mat3x3.hpp"
#include "glm/mat4x4.hpp"
#include "glm/vec3.hpp"
#include "glm/vec4.hpp"
#include <cstdint>

#if PLATFORM_COMPILER_MSVC
#pragma warning(pop)
#endif

namespace iris::Renderer {

extern VkInstance sInstance;
extern VkDebugUtilsMessengerEXT sDebugUtilsMessenger;
extern VkPhysicalDevice sPhysicalDevice;
extern VkDevice sDevice;
extern VmaAllocator sAllocator;
extern VkRenderPass sRenderPass;

extern VkSurfaceFormatKHR const sSurfaceColorFormat;
extern VkFormat const sSurfaceDepthStencilFormat;
extern VkSampleCountFlagBits const sSurfaceSampleCount;
extern VkPresentModeKHR const sSurfacePresentMode;

extern VkDescriptorPool sDescriptorPool;
extern VkDescriptorSetLayout sGlobalDescriptorSetLayout;

extern std::uint32_t const sCommandQueueGraphics;

extern std::uint32_t const sNumRenderPassAttachments;
extern std::uint32_t const sColorTargetAttachmentIndex;
extern std::uint32_t const sColorResolveAttachmentIndex;
extern std::uint32_t const sDepthStencilTargetAttachmentIndex;
extern std::uint32_t const sDepthStencilResolveAttachmentIndex;

template <class T>
void NameObject(VkObjectType objectType [[maybe_unused]],
                T objectHandle [[maybe_unused]],
                gsl::czstring<> objectName [[maybe_unused]]) noexcept {
  Expects(sDevice != VK_NULL_HANDLE);
#if 0 // this doesn't work outside the debugger?
  VkDebugUtilsObjectNameInfoEXT objectNameInfo = {
    VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT, nullptr, objectType,
    reinterpret_cast<std::uint64_t>(objectHandle), objectName};
  vkSetDebugUtilsObjectNameEXT(sDevice, &objectNameInfo);
#endif
} // NameObject

struct PushConstants {
    glm::vec4 iMouse;
    float iTime;
    float iTimeDelta;
    float iFrameRate;
    float iFrame;
    glm::vec3 iResolution;
    float padding0;
    glm::vec4 EyePosition;
    glm::mat4 ModelMatrix;
    glm::mat4 ModelViewMatrix;
    glm::mat3 NormalMatrix;
}; // struct PushConstants

struct MatricesBuffer {
  glm::mat4 ViewMatrix;
  glm::mat4 ViewMatrixInverse;
  glm::mat4 ProjectionMatrix;
  glm::mat4 ProjectionMatrixInverse;
}; // struct MatricesBuffer

#define MAX_LIGHTS 100

struct Light {
  glm::vec4 direction;
  glm::vec4 color;
}; // struct Light

struct LightsBuffer {
  Light Lights[MAX_LIGHTS];
  int NumLights;
}; // struct LightsBuffer

} // namespace iris::Renderer

#endif // HEV_IRIS_RENDERER_UTIL_H_
