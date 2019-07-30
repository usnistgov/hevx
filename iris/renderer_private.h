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

inline constexpr VkSurfaceFormatKHR const sSurfaceColorFormat{
  VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
inline constexpr VkFormat const sSurfaceDepthStencilFormat{
  VK_FORMAT_D32_SFLOAT};
inline constexpr VkSampleCountFlagBits const sSurfaceSampleCount{
  VK_SAMPLE_COUNT_4_BIT};
inline constexpr VkPresentModeKHR const sSurfacePresentMode{
  VK_PRESENT_MODE_FIFO_KHR};

extern VkDescriptorPool sDescriptorPool;
extern VkDescriptorSetLayout sGlobalDescriptorSetLayout;

inline constexpr std::uint32_t const sCommandQueueGraphics{0};

inline constexpr std::uint32_t const sNumRenderPassAttachments{4};
inline constexpr std::uint32_t const sColorTargetAttachmentIndex{0};
inline constexpr std::uint32_t const sColorResolveAttachmentIndex{1};
inline constexpr std::uint32_t const sDepthStencilTargetAttachmentIndex{2};
inline constexpr std::uint32_t const sDepthStencilResolveAttachmentIndex{3};

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
    bool bDebugNormals;
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
