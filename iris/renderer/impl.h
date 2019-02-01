#ifndef HEV_IRIS_RENDERER_IMPL_H_
#define HEV_IRIS_RENDERER_IMPL_H_

#include "absl/container/fixed_array.h"
#include "config.h"
#include "error.h"
#include "glm/mat4x4.hpp"
#include "gsl/gsl"
#include "renderer/renderer.h"
#include "renderer/vulkan.h"
#include <cstdint>
#include <system_error>
#include <vector>

namespace iris::Renderer {

extern VkInstance sInstance;
extern VkPhysicalDevice sPhysicalDevice;
extern std::uint32_t sGraphicsQueueFamilyIndex;
extern VkDevice sDevice;
extern VkQueue sGraphicsCommandQueue;
extern VkFence sFrameComplete;
extern VmaAllocator sAllocator;

// These are the desired properties of all surfaces for the renderer.
extern VkSurfaceFormatKHR sSurfaceColorFormat;
extern VkFormat sSurfaceDepthStencilFormat;
extern VkSampleCountFlagBits sSurfaceSampleCount;
extern VkPresentModeKHR sSurfacePresentMode;

extern std::uint32_t sNumRenderPassAttachments;
extern std::uint32_t sColorTargetAttachmentIndex;
extern std::uint32_t sColorResolveAttachmentIndex;
extern std::uint32_t sDepthStencilTargetAttachmentIndex;
extern std::uint32_t sDepthStencilResolveAttachmentIndex;

extern VkRenderPass sRenderPass;
extern VkDescriptorSetLayout sBaseDescriptorSetLayout;

[[nodiscard]] tl::expected<VkCommandBuffer, std::system_error>
BeginOneTimeSubmit(VkCommandPool commandPool = VK_NULL_HANDLE) noexcept;

[[nodiscard]] std::system_error
EndOneTimeSubmit(VkCommandBuffer commandBuffer,
                 VkCommandPool commandPool = VK_NULL_HANDLE) noexcept;

template <class T>
void NameObject(VkObjectType objectType, T objectHandle,
                gsl::czstring<> objectName) noexcept {
  VkDebugUtilsObjectNameInfoEXT objectNameInfo = {
    VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT, nullptr, objectType,
    reinterpret_cast<std::uint64_t>(objectHandle), objectName};
  vkSetDebugUtilsObjectNameEXT(sDevice, &objectNameInfo);
} // NameObject

struct CommandBuffers;
[[nodiscard]] tl::expected<CommandBuffers, std::system_error>
AllocateCommandBuffers(
  std::uint32_t count,
  VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY) noexcept;

struct DescriptorSets;
[[nodiscard]] tl::expected<DescriptorSets, std::system_error>
AllocateDescriptorSets(gsl::span<VkDescriptorSetLayoutBinding> bindings,
                       std::uint32_t numSets, std::string name = {}) noexcept;

struct MeshData;
[[nodiscard]] std::system_error
CreateMeshes(gsl::span<const MeshData> meshData) noexcept;

} // namespace iris::Renderer

#endif // HEV_IRIS_RENDERER_IMPL_H_

