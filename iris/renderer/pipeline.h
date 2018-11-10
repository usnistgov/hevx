#ifndef HEV_IRIS_RENDERER_PIPELINE_H_
#define HEV_IRIS_RENDERER_PIPELINE_H_

#include "iris/renderer/impl.h"
#include "iris/renderer/shader.h"
#include <cstdint>
#include <string>
#include <system_error>

namespace iris::Renderer {

struct Pipeline {
  static tl::expected<Pipeline, std::system_error> CreateGraphics(
    gsl::span<VkDescriptorSetLayout> descriptorSetLayouts,
    gsl::span<VkPushConstantRange> pushConstantRanges,
    gsl::span<Shader> shaders,
    gsl::span<VkVertexInputBindingDescription> vertexInputBindingDescriptions,
    gsl::span<VkVertexInputAttributeDescription>
      vertexInputAttributeDescriptions,
    VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI,
    VkPipelineViewportStateCreateInfo viewportStateCI,
    VkPipelineRasterizationStateCreateInfo rasterizationStateCI,
    VkPipelineMultisampleStateCreateInfo multisampleStateCI,
    VkPipelineDepthStencilStateCreateInfo depthStencilStateCI,
    gsl::span<VkPipelineColorBlendAttachmentState> colorBlendAttachmentStates,
    gsl::span<VkDynamicState> dynamicStates, std::uint32_t renderPassSubpass,
    std::string name = {}) noexcept;

  VkPipelineLayout layout{VK_NULL_HANDLE};
  VkPipeline handle{VK_NULL_HANDLE};

  operator VkPipeline() const noexcept { return handle; }

  Pipeline() = default;
  Pipeline(Pipeline const&) = delete;
  Pipeline(Pipeline&& other) noexcept;
  Pipeline& operator=(Pipeline const&) = delete;
  Pipeline& operator=(Pipeline&& rhs) noexcept;
  ~Pipeline() noexcept;

private:
  std::string name;
}; // struct Pipeline

} // namespace iris::Renderer

#endif // HEV_IRIS_RENDERER_PIPELINE_H_
