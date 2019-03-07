#ifndef HEV_IRIS_RENDERER_UTIL_H_
#define HEV_IRIS_RENDERER_UTIL_H_

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

struct ShaderToyPushConstants {
    glm::vec4 iMouse;
    float iTime;
    float iTimeDelta;
    float iFrameRate;
    float iFrame;
    glm::vec3 iResolution;
    float padding0;
}; // struct ShaderToyPushConstants

tl::expected<std::pair<VkPipelineLayout, VkPipeline>, std::system_error>
CreateGraphicsPipeline(
  gsl::span<const VkDescriptorSetLayout> descriptorSetLayouts,
  gsl::span<const VkPushConstantRange> pushConstantRanges,
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
  std::uint32_t renderPassSubpass, std::string name = {}) noexcept;

} // namespace iris::Renderer

#endif // HEV_IRIS_RENDERER_UTIL_H_
