#include "renderer/pipeline.h"
#include "logging.h"

tl::expected<iris::Renderer::Pipeline, std::system_error>
iris::Renderer::Pipeline::CreateGraphics(
  gsl::span<VkDescriptorSetLayout> descriptorSetLayouts,
  gsl::span<VkPushConstantRange> pushConstantRanges, gsl::span<Shader> shaders,
  gsl::span<VkVertexInputBindingDescription> vertexInputBindingDescriptions,
  gsl::span<VkVertexInputAttributeDescription> vertexInputAttributeDescriptions,
  VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI,
  VkPipelineViewportStateCreateInfo viewportStateCI,
  VkPipelineRasterizationStateCreateInfo rasterizationStateCI,
  VkPipelineMultisampleStateCreateInfo multisampleStateCI,
  VkPipelineDepthStencilStateCreateInfo depthStencilStateCI,
  gsl::span<VkPipelineColorBlendAttachmentState> colorBlendAttachmentStates,
  gsl::span<VkDynamicState> dynamicStates, std::uint32_t renderPassSubpass,
  std::string name) noexcept {
  IRIS_LOG_ENTER();
  Expects(sDevice != VK_NULL_HANDLE);
  Expects(sRenderPass != VK_NULL_HANDLE);

  Pipeline pipeline;

  VkPipelineLayoutCreateInfo pipelineLayoutCI = {};
  pipelineLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutCI.setLayoutCount =
    gsl::narrow_cast<std::uint32_t>(descriptorSetLayouts.size());
  pipelineLayoutCI.pSetLayouts = descriptorSetLayouts.data();
  pipelineLayoutCI.pushConstantRangeCount =
    gsl::narrow_cast<std::uint32_t>(pushConstantRanges.size());
  pipelineLayoutCI.pPushConstantRanges = pushConstantRanges.data();

  if (auto result = vkCreatePipelineLayout(sDevice, &pipelineLayoutCI, nullptr,
                                           &pipeline.layout);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(make_error_code(result),
                                            "Cannot create pipeline layout"));
  }

  absl::FixedArray<VkPipelineShaderStageCreateInfo> shaderStageCIs(
    shaders.size());
  for (std::ptrdiff_t i = 0; i < shaders.size(); ++i) {
    shaderStageCIs[i] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                         nullptr,
                         0,
                         shaders[i].stage,
                         shaders[i].handle,
                         shaders[i].entry.c_str(),
                         nullptr};
  }

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
  graphicsPipelineCI.stageCount = static_cast<uint32_t>(shaderStageCIs.size());
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
  graphicsPipelineCI.renderPass = sRenderPass;
  graphicsPipelineCI.subpass = renderPassSubpass;

  if (auto result = vkCreateGraphicsPipelines(sDevice, VK_NULL_HANDLE, 1,
                                              &graphicsPipelineCI, nullptr,
                                              &pipeline.handle);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(make_error_code(result),
                                            "Cannot create graphics pipeline"));
  }

  if (!name.empty()) {
    NameObject(VK_OBJECT_TYPE_PIPELINE_LAYOUT, pipeline.layout,
               (name + ".layout").c_str());
    NameObject(VK_OBJECT_TYPE_PIPELINE, pipeline.handle, name.c_str());
  }

  pipeline.name = std::move(name);

  Ensures(pipeline.layout != VK_NULL_HANDLE);
  Ensures(pipeline.handle != VK_NULL_HANDLE);
  IRIS_LOG_LEAVE();
  return std::move(pipeline);
} // iris::Renderer::Pipeline::Create

iris::Renderer::Pipeline::Pipeline(Pipeline&& other) noexcept
  : layout(other.layout)
  , handle(other.handle)
  , name(std::move(other.name)) {
  other.layout = VK_NULL_HANDLE;
  other.handle = VK_NULL_HANDLE;
} // iris::Renderer::Pipeline::Pipeline

iris::Renderer::Pipeline& iris::Renderer::Pipeline::
operator=(Pipeline&& rhs) noexcept {
  if (this == &rhs) return *this;

  layout = rhs.layout;
  handle = rhs.handle;
  name = std::move(rhs.name);

  rhs.layout = VK_NULL_HANDLE;
  rhs.handle = VK_NULL_HANDLE;

  return *this;
}

iris::Renderer::Pipeline::~Pipeline() noexcept {
  if (layout == VK_NULL_HANDLE && handle == VK_NULL_HANDLE) return;
  IRIS_LOG_ENTER();
  Expects(sDevice != VK_NULL_HANDLE);

  vkDestroyPipelineLayout(sDevice, layout, nullptr);
  vkDestroyPipeline(sDevice, handle, nullptr);

  IRIS_LOG_LEAVE();
}
