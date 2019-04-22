#include "pipeline.h"
#include "config.h"

#include "absl/container/fixed_array.h"
#include "error.h"
#include "logging.h"
#include "renderer_private.h"

tl::expected<iris::Pipeline, std::system_error>
iris::CreateRasterizationPipeline(
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
  gsl::span<const VkDescriptorSetLayout> descriptorSetLayouts) noexcept {
  IRIS_LOG_ENTER();
  Expects(Renderer::sDevice != VK_NULL_HANDLE);
  Expects(Renderer::sRenderPass != VK_NULL_HANDLE);
  Expects(Renderer::sGlobalDescriptorSetLayout != VK_NULL_HANDLE);

  Pipeline pipeline;

  absl::InlinedVector<VkDescriptorSetLayout, 8> allDescriptorSetLayouts;
  allDescriptorSetLayouts.push_back(Renderer::sGlobalDescriptorSetLayout);
  std::copy_n(std::begin(descriptorSetLayouts), std::size(descriptorSetLayouts),
              std::back_inserter(allDescriptorSetLayouts));

  VkPushConstantRange pushConstantRange = {};
  pushConstantRange.stageFlags =
    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  pushConstantRange.offset = 0;
  pushConstantRange.size = sizeof(Renderer::PushConstants);

  VkPipelineLayoutCreateInfo pipelineLayoutCI = {};
  pipelineLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutCI.setLayoutCount =
    gsl::narrow_cast<std::uint32_t>(allDescriptorSetLayouts.size());
  pipelineLayoutCI.pSetLayouts = allDescriptorSetLayouts.data();
  pipelineLayoutCI.pushConstantRangeCount = 1;
  pipelineLayoutCI.pPushConstantRanges = &pushConstantRange;

  if (auto result = vkCreatePipelineLayout(Renderer::sDevice, &pipelineLayoutCI,
                                           nullptr, &pipeline.layout);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(make_error_code(result),
                                            "Cannot create pipeline layout"));
  }

  absl::FixedArray<VkPipelineShaderStageCreateInfo> shaderStageCIs(
    shaders.size());
  std::transform(shaders.begin(), shaders.end(), shaderStageCIs.begin(),
                 [](Shader const& shader) {
                   return VkPipelineShaderStageCreateInfo{
                     VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                     nullptr,
                     0,
                     shader.stage,
                     shader.module,
                     "main",
                     nullptr};
                 });

  VkPipelineVertexInputStateCreateInfo vertexInputStateCI = {};
  vertexInputStateCI.sType =
    VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertexInputStateCI.vertexBindingDescriptionCount =
    gsl::narrow_cast<std::uint32_t>(vertexInputBindingDescriptions.size());
  vertexInputStateCI.pVertexBindingDescriptions =
    vertexInputBindingDescriptions.data();
  vertexInputStateCI.vertexAttributeDescriptionCount =
    gsl::narrow_cast<std::uint32_t>(vertexInputAttributeDescriptions.size());
  vertexInputStateCI.pVertexAttributeDescriptions =
    vertexInputAttributeDescriptions.data();

  VkPipelineColorBlendStateCreateInfo colorBlendStateCI = {};
  colorBlendStateCI.sType =
    VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlendStateCI.attachmentCount =
    gsl::narrow_cast<std::uint32_t>(colorBlendAttachmentStates.size());
  colorBlendStateCI.pAttachments = colorBlendAttachmentStates.data();

  VkPipelineDynamicStateCreateInfo dynamicStateCI = {};
  dynamicStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamicStateCI.dynamicStateCount =
    gsl::narrow_cast<uint32_t>(dynamicStates.size());
  dynamicStateCI.pDynamicStates = dynamicStates.data();

  VkGraphicsPipelineCreateInfo graphicsPipelineCI = {};
  graphicsPipelineCI.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  graphicsPipelineCI.stageCount =
    gsl::narrow_cast<std::uint32_t>(shaderStageCIs.size());
  graphicsPipelineCI.pStages = shaderStageCIs.data();
  graphicsPipelineCI.pVertexInputState = &vertexInputStateCI;
  graphicsPipelineCI.pInputAssemblyState = &inputAssemblyStateCI;
  graphicsPipelineCI.pViewportState = &viewportStateCI;
  graphicsPipelineCI.pRasterizationState = &rasterizationStateCI;
  graphicsPipelineCI.pMultisampleState = &multisampleStateCI;
  graphicsPipelineCI.pDepthStencilState = &depthStencilStateCI;
  graphicsPipelineCI.pColorBlendState = &colorBlendStateCI;
  graphicsPipelineCI.pDynamicState = &dynamicStateCI;
  graphicsPipelineCI.layout = pipeline.layout;
  graphicsPipelineCI.renderPass = Renderer::sRenderPass;
  graphicsPipelineCI.subpass = renderPassSubpass;

  if (auto result = vkCreateGraphicsPipelines(Renderer::sDevice, VK_NULL_HANDLE,
                                              1, &graphicsPipelineCI, nullptr,
                                              &pipeline.pipeline);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    vkDestroyPipelineLayout(Renderer::sDevice, pipeline.layout, nullptr);
    return tl::unexpected(std::system_error(make_error_code(result),
                                            "Cannot create graphics pipeline"));
  }

  Ensures(pipeline.layout != VK_NULL_HANDLE);
  Ensures(pipeline.pipeline != VK_NULL_HANDLE);

  IRIS_LOG_LEAVE();
  return pipeline;
} // iris::CreateRasterizationPipeline

tl::expected<iris::Pipeline, std::system_error> iris::CreateRayTracingPipeline(
  gsl::span<const Shader> shaders, gsl::span<const ShaderGroup> groups,
  gsl::span<const VkDescriptorSetLayout> descriptorSetLayouts,
  std::uint32_t maxRecursionDepth) noexcept {
  IRIS_LOG_ENTER();
  Expects(Renderer::sDevice != VK_NULL_HANDLE);
  Expects(Renderer::sGlobalDescriptorSetLayout != VK_NULL_HANDLE);

  Pipeline pipeline;

  absl::InlinedVector<VkDescriptorSetLayout, 8> allDescriptorSetLayouts;
  allDescriptorSetLayouts.push_back(Renderer::sGlobalDescriptorSetLayout);
  std::copy_n(std::begin(descriptorSetLayouts), std::size(descriptorSetLayouts),
              std::back_inserter(allDescriptorSetLayouts));

  VkPushConstantRange pushConstantRange = {};
  pushConstantRange.stageFlags =
    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  pushConstantRange.offset = 0;
  pushConstantRange.size = sizeof(Renderer::PushConstants);

  VkPipelineLayoutCreateInfo pipelineLayoutCI = {};
  pipelineLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutCI.setLayoutCount =
    gsl::narrow_cast<std::uint32_t>(allDescriptorSetLayouts.size());
  pipelineLayoutCI.pSetLayouts = allDescriptorSetLayouts.data();
  pipelineLayoutCI.pushConstantRangeCount = 1;
  pipelineLayoutCI.pPushConstantRanges = &pushConstantRange;

  if (auto result = vkCreatePipelineLayout(Renderer::sDevice, &pipelineLayoutCI,
                                           nullptr, &pipeline.layout);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(make_error_code(result),
                                            "Cannot create pipeline layout"));
  }

  absl::FixedArray<VkPipelineShaderStageCreateInfo> shaderStageCIs(
    shaders.size());
  std::transform(shaders.begin(), shaders.end(), shaderStageCIs.begin(),
                 [](Shader const& shader) {
                   return VkPipelineShaderStageCreateInfo{
                     VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                     nullptr,
                     0,
                     shader.stage,
                     shader.module,
                     "main",
                     nullptr};
                 });

  absl::FixedArray<VkRayTracingShaderGroupCreateInfoNV> shaderGroupCIs(
    groups.size());
  std::transform(groups.begin(), groups.end(), shaderGroupCIs.begin(),
                 [](ShaderGroup const& group) {
                   return VkRayTracingShaderGroupCreateInfoNV{
                     VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_NV,
                     nullptr,
                     group.type,
                     group.generalShaderIndex,
                     group.closestHitShaderIndex,
                     group.anyHitShaderIndex,
                     group.intersectionShaderIndex};
                 });

  VkRayTracingPipelineCreateInfoNV pipelineCI = {};
  pipelineCI.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_NV;
  pipelineCI.stageCount =
    gsl::narrow_cast<std::uint32_t>(shaderStageCIs.size());
  pipelineCI.pStages = shaderStageCIs.data();
  pipelineCI.groupCount =
    gsl::narrow_cast<std::uint32_t>(shaderGroupCIs.size());
  pipelineCI.pGroups = shaderGroupCIs.data();
  pipelineCI.maxRecursionDepth = maxRecursionDepth;
  pipelineCI.layout = pipeline.layout;

  if (auto result =
        vkCreateRayTracingPipelinesNV(Renderer::sDevice, VK_NULL_HANDLE, 1,
                                      &pipelineCI, nullptr, &pipeline.pipeline);
      result != VK_SUCCESS) {
    return tl::unexpected(std::system_error(iris::make_error_code(result),
                                            "Cannot create pipeline"));
  }

  Ensures(pipeline.layout != VK_NULL_HANDLE);
  Ensures(pipeline.pipeline != VK_NULL_HANDLE);

  IRIS_LOG_LEAVE();
  return pipeline;
} // iris::CreateRayTracingPipeline

