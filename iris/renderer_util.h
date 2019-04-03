#ifndef HEV_IRIS_RENDERER_UTIL_H_
#define HEV_IRIS_RENDERER_UTIL_H_

#if PLATFORM_COMPILER_GCC
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#include "absl/container/inlined_vector.h"
#include "glm/mat3x3.hpp"
#include "glm/mat4x4.hpp"
#include "glm/vec3.hpp"
#include "glm/vec4.hpp"
#include "iris/shader.h"
#include "iris/vulkan_util.h"
#include <cstdint>

namespace iris::Renderer {

extern VkInstance sInstance;
extern VkDebugUtilsMessengerEXT sDebugUtilsMessenger;
extern VkPhysicalDevice sPhysicalDevice;
extern VkDevice sDevice;
extern VmaAllocator sAllocator;
extern VkRenderPass sRenderPass;

// These are for use in window.cc
extern std::uint32_t sQueueFamilyIndex;
extern absl::InlinedVector<VkQueue, 16> sCommandQueues;
extern absl::InlinedVector<VkCommandPool, 16> sCommandPools;
extern absl::InlinedVector<VkFence, 16> sCommandFences;

extern VkSurfaceFormatKHR const sSurfaceColorFormat;
extern VkFormat const sSurfaceDepthStencilFormat;
extern VkSampleCountFlagBits const sSurfaceSampleCount;
extern VkPresentModeKHR const sSurfacePresentMode;

extern VkDescriptorPool sDescriptorPool;
extern VkDescriptorSetLayout sGlobalDescriptorSetLayout;

extern std::uint32_t const sNumRenderPassAttachments;
extern std::uint32_t const sColorTargetAttachmentIndex;
extern std::uint32_t const sColorResolveAttachmentIndex;
extern std::uint32_t const sDepthStencilTargetAttachmentIndex;
extern std::uint32_t const sDepthStencilResolveAttachmentIndex;

[[nodiscard]] tl::expected<VkCommandBuffer, std::system_error>
BeginOneTimeSubmit(VkCommandPool commandPool) noexcept;

tl::expected<void, std::system_error>
EndOneTimeSubmit(VkCommandBuffer commandBuffer, VkCommandPool commandPool,
                 VkQueue queue, VkFence fence) noexcept;

struct AccelerationStructure {
  VkAccelerationStructureNV structure{VK_NULL_HANDLE};
  VmaAllocation allocation{VK_NULL_HANDLE};
}; // struct AccelerationStructure

struct GeometryInstance {
  float transform[12];
  std::uint32_t customIndex : 24;
  std::uint32_t mask : 8;
  std::uint32_t offset : 24;
  std::uint32_t flags : 8;
  std::uint64_t accelerationStructureHandle;

  GeometryInstance(std::uint64_t handle = 0) noexcept
    : accelerationStructureHandle(handle) {
    customIndex = 0;
    mask = 0xF;
    offset = 0;
    flags = 0;
  }
}; // GeometryInstance

[[nodiscard]] tl::expected<AccelerationStructure, std::system_error>
CreateAccelerationStructure(
  VkAccelerationStructureInfoNV const& accelerationStructureInfo,
  VkDeviceSize compactedSize) noexcept;

template <class T>
void NameObject(VkObjectType objectType [[maybe_unused]],
                T objectHandle [[maybe_unused]],
                gsl::czstring<> objectName [[maybe_unused]]) noexcept {
  Expects(sDevice != VK_NULL_HANDLE);
#if 0 // doesn't work with 1.1.101 validation layers??
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
    glm::mat4 ModelMatrix;
    glm::mat4 ModelViewMatrix;
    glm::mat4 ModelViewMatrixInverse;
    //glm::mat3 NormalMatrix;
}; // struct PushConstants

struct MatricesBuffer {
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

struct Pipeline {
  VkPipelineLayout layout{VK_NULL_HANDLE};
  VkPipeline pipeline{VK_NULL_HANDLE};
}; // struct Pipeline

tl::expected<Pipeline, std::system_error> CreateRasterizationPipeline(
  gsl::span<const Shader> shaders,
  gsl::span<const VkVertexInputBindingDescription>
    vertexInputBindingDescriptions,
  gsl::span<const VkVertexInputAttributeDescription>
    vertexInputAttributeDescriptions,
  VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI,
  VkPipelineViewportStateCreateInfo viewportStateCI,
  VkPipelineRasterizationStateCreateInfo rasterizationStateCI,
  VkPipelineMultisampleStateCreateInfo multisampleStateCI,
  VkPipelineDepthStencilStateCreateInfo depthStencilStateCI,
  gsl::span<const VkPipelineColorBlendAttachmentState>
    colorBlendAttachmentStates,
  gsl::span<const VkDynamicState> dynamicStates,
  std::uint32_t renderPassSubpass,
  gsl::span<const VkDescriptorSetLayout> descriptorSetLayouts) noexcept;

struct ShaderGroup {
  VkRayTracingShaderGroupTypeNV type;
  std::uint32_t generalShaderIndex{VK_SHADER_UNUSED_NV};
  std::uint32_t closestHitShaderIndex{VK_SHADER_UNUSED_NV};
  std::uint32_t anyHitShaderIndex{VK_SHADER_UNUSED_NV};
  std::uint32_t intersectionShaderIndex{VK_SHADER_UNUSED_NV};
}; // struct ShaderGroup

tl::expected<Pipeline, std::system_error> CreateRayTracingPipeline(
  gsl::span<const Shader> shaders, gsl::span<const ShaderGroup> groups,
  gsl::span<const VkDescriptorSetLayout> descriptorSetLayouts,
  std::uint32_t maxRecursionDepth) noexcept;

} // namespace iris::Renderer

#endif // HEV_IRIS_RENDERER_UTIL_H_
