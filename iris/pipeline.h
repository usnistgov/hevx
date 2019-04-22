#ifndef HEV_IRIS_PIPELINE_H_
#define HEV_IRIS_PIPELINE_H_

#include "iris/config.h"

#include "iris/shader.h"
#include "iris/vulkan.h"

#if PLATFORM_COMPILER_MSVC
#include <codeanalysis/warnings.h>
#pragma warning(push)
#pragma warning(disable: ALL_CODE_ANALYSIS_WARNINGS)
#pragma warning(disable: ALL_CPPCORECHECK_WARNINGS)
#endif

#include "expected.hpp"
#include "gsl/gsl"
#include <system_error>

#if PLATFORM_COMPILER_MSVC
#pragma warning(pop)
#endif

namespace iris {

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

tl::expected<Pipeline, std::system_error> CreateRayTracingPipeline(
  gsl::span<const Shader> shaders, gsl::span<const ShaderGroup> groups,
  gsl::span<const VkDescriptorSetLayout> descriptorSetLayouts,
  std::uint32_t maxRecursionDepth) noexcept;

} // namespace iris

#endif // HEV_IRIS_PIPELINE_H_

