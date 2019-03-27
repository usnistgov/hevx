#ifndef HEV_IRIS_RENDERER_UTIL_H_
#define HEV_IRIS_RENDERER_UTIL_H_

#include "glm/mat3x3.hpp"
#include "glm/mat4x4.hpp"
#include "glm/vec3.hpp"
#include "glm/vec4.hpp"
#include "iris/vulkan_util.h"

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

[[nodiscard]] tl::expected<VkCommandBuffer, std::system_error>
BeginOneTimeSubmit(VkCommandPool commandPool) noexcept;

tl::expected<void, std::system_error>
EndOneTimeSubmit(VkCommandBuffer commandBuffer, VkCommandPool commandPool,
                 VkQueue queue, VkFence fence) noexcept;

template <class T>
tl::expected<T, std::system_error>
MapMemory(VmaAllocation allocation) noexcept {
  void* ptr;
  if (auto result = vmaMapMemory(sAllocator, allocation, &ptr);
      result != VK_SUCCESS) {
    return tl::unexpected(
      std::system_error(make_error_code(result), "Cannot map memory"));
  }
  return reinterpret_cast<T>(ptr);
}

inline void UnmapMemory(VmaAllocation allocation) noexcept {
  vmaUnmapMemory(sAllocator, allocation);
}

tl::expected<void, std::system_error>
TransitionImage(VkCommandPool commandPool, VkQueue queue, VkFence fence,
                VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
                std::uint32_t mipLevels, std::uint32_t arrayLayers) noexcept;

[[nodiscard]] tl::expected<std::tuple<VkImage, VmaAllocation, VkImageView>,
                           std::system_error>
AllocateImageAndView(VkFormat format, VkExtent2D extent,
                     std::uint32_t mipLevels, std::uint32_t arrayLayers,
                     VkSampleCountFlagBits sampleCount,
                     VkImageUsageFlags imageUsage, VkImageTiling imageTiling,
                     VmaMemoryUsage memoryUsage,
                     VkImageSubresourceRange subresourceRange) noexcept;

[[nodiscard]] tl::expected<std::tuple<VkImage, VmaAllocation>,
                           std::system_error>
CreateImage(VkCommandPool commandPool, VkQueue queue, VkFence fence,
            VkFormat format, VkExtent2D extent, VkImageUsageFlags imageUsage,
            VmaMemoryUsage memoryUsage, gsl::not_null<std::byte*> pixels,
            std::uint32_t bytesPerPixel) noexcept;

[[nodiscard]] tl::expected<std::tuple<VkBuffer, VmaAllocation, VkDeviceSize>,
                           std::system_error>
AllocateBuffer(VkDeviceSize size, VkBufferUsageFlags bufferUsage,
               VmaMemoryUsage memoryUsage) noexcept;

[[nodiscard]] tl::expected<std::tuple<VkBuffer, VmaAllocation, VkDeviceSize>,
                           std::system_error>
ReallocateBuffer(VkBuffer buffer, VmaAllocation allocation,
                 VkDeviceSize oldSize, VkDeviceSize newSize,
                 VkBufferUsageFlags bufferUsage,
                 VmaMemoryUsage memoryUsage) noexcept;

[[nodiscard]] tl::expected<std::tuple<VkBuffer, VmaAllocation, VkDeviceSize>,
                           std::system_error>
CreateBuffer(VkCommandPool commandPool, VkQueue queue, VkFence fence,
             VkBufferUsageFlags bufferUsage, VmaMemoryUsage memoryUsage,
             VkDeviceSize size, gsl::not_null<std::byte*> data) noexcept;

inline void DestroyBuffer(VkBuffer buffer, VmaAllocation allocation) noexcept {
  vmaDestroyBuffer(sAllocator, buffer, allocation);
}

struct Shader {
  VkShaderModule handle;
  VkShaderStageFlagBits stage;
}; // struct Shader

[[nodiscard]] tl::expected<VkShaderModule, std::system_error>
CompileShaderFromSource(std::string_view source,
                        VkShaderStageFlagBits stage) noexcept;

[[nodiscard]] tl::expected<VkShaderModule, std::system_error>
LoadShaderFromFile(filesystem::path const& path,
                   VkShaderStageFlagBits stage) noexcept;

[[nodiscard]] tl::expected<std::tuple<VkAccelerationStructureNV, VmaAllocation>,
                           std::system_error>
CreateAccelerationStructure(VkAccelerationStructureCreateInfoNV*
                              pAccelerationStructureCreateInfo) noexcept;

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

tl::expected<std::pair<VkPipelineLayout, VkPipeline>, std::system_error>
CreateGraphicsPipeline(
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

} // namespace iris::Renderer

#endif // HEV_IRIS_RENDERER_UTIL_H_
