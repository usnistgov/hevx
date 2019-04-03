#include "renderer_util.h"
#if PLATFORM_COMPILER_GCC
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#include "absl/container/inlined_vector.h"
#include "enumerate.h"
#include "error.h"
#include "renderer.h"
#include "logging.h"
#include "vulkan_util.h"

tl::expected<VkCommandBuffer, std::system_error>
iris::Renderer::BeginOneTimeSubmit(VkCommandPool commandPool) noexcept {
  IRIS_LOG_ENTER();
  Expects(sDevice != VK_NULL_HANDLE);
  Expects(commandPool != VK_NULL_HANDLE);

  VkCommandBufferAllocateInfo commandBufferAI = {};
  commandBufferAI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  commandBufferAI.commandPool = commandPool;
  commandBufferAI.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  commandBufferAI.commandBufferCount = 1;

  VkCommandBuffer commandBuffer;
  if (auto result =
        vkAllocateCommandBuffers(sDevice, &commandBufferAI, &commandBuffer);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(make_error_code(result),
                                            "Cannot allocate command buffer"));
  }

  VkCommandBufferBeginInfo commandBufferBI = {};
  commandBufferBI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  commandBufferBI.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

  if (auto result = vkBeginCommandBuffer(commandBuffer, &commandBufferBI);
      result != VK_SUCCESS) {
    vkFreeCommandBuffers(sDevice, commandPool, 1, &commandBuffer);
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(make_error_code(result),
                                            "Cannot begin command buffer"));
  }

  IRIS_LOG_LEAVE();
  return commandBuffer;
} // iris::Renderer::BeginOneTimeSubmit

tl::expected<void, std::system_error>
iris::Renderer::EndOneTimeSubmit(VkCommandBuffer commandBuffer,
                                 VkCommandPool commandPool, VkQueue queue,
                                 VkFence fence) noexcept {
  IRIS_LOG_ENTER();
  Expects(sDevice != VK_NULL_HANDLE);
  Expects(commandBuffer != VK_NULL_HANDLE);
  Expects(commandPool != VK_NULL_HANDLE);
  Expects(queue != VK_NULL_HANDLE);
  Expects(fence != VK_NULL_HANDLE);

  VkSubmitInfo submitI = {};
  submitI.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitI.commandBufferCount = 1;
  submitI.pCommandBuffers = &commandBuffer;

  if (auto result = vkEndCommandBuffer(commandBuffer); result != VK_SUCCESS) {
    vkFreeCommandBuffers(sDevice, commandPool, 1, &commandBuffer);
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(make_error_code(result), "Cannot end command buffer"));
  }

  if (auto result = vkQueueSubmit(queue, 1, &submitI, fence);
      result != VK_SUCCESS) {
    vkFreeCommandBuffers(sDevice, commandPool, 1, &commandBuffer);
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(make_error_code(result),
                                            "Cannot submit command buffer"));
  }

  if (auto result = vkWaitForFences(sDevice, 1, &fence, VK_TRUE, UINT64_MAX);
      result != VK_SUCCESS) {
    vkFreeCommandBuffers(sDevice, commandPool, 1, &commandBuffer);
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(
      make_error_code(result), "Cannot wait on one-time submit fence"));
  }

  if (auto result = vkResetFences(sDevice, 1, &fence); result != VK_SUCCESS) {
    vkFreeCommandBuffers(sDevice, commandPool, 1, &commandBuffer);
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(
      make_error_code(result), "Cannot reset one-time submit fence"));
  }

  vkFreeCommandBuffers(sDevice, commandPool, 1, &commandBuffer);
  IRIS_LOG_LEAVE();
  return {};
} // iris::Renderer::EndOneTimeSubmit

tl::expected<iris::Renderer::AccelerationStructure, std::system_error>
iris::Renderer::CreateAccelerationStructure(
  VkAccelerationStructureInfoNV const& accelerationStructureInfo,
  VkDeviceSize compactedSize) noexcept {
  IRIS_LOG_ENTER();
  Expects(sDevice != VK_NULL_HANDLE);
  Expects(sAllocator != VK_NULL_HANDLE);

  VkAccelerationStructureCreateInfoNV accelerationStructureCI = {};
  accelerationStructureCI.sType =
    VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_NV;
  accelerationStructureCI.compactedSize = compactedSize;
  accelerationStructureCI.info = accelerationStructureInfo;

  AccelerationStructure structure;
  if (auto result = vkCreateAccelerationStructureNV(
        sDevice, &accelerationStructureCI, nullptr, &structure.structure);
      result != VK_SUCCESS) {
    return tl::unexpected(std::system_error(
      make_error_code(result), "Cannot create acceleration structure"));
  }

  VkMemoryRequirements2KHR memoryRequirements = {};
  memoryRequirements.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2_KHR;

  VkAccelerationStructureMemoryRequirementsInfoNV memoryRequirementsInfo = {};
  memoryRequirementsInfo.sType =
    VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_INFO_NV;
  memoryRequirementsInfo.accelerationStructure = structure.structure;
  memoryRequirementsInfo.type =
    VK_ACCELERATION_STRUCTURE_MEMORY_REQUIREMENTS_TYPE_OBJECT_NV;
  vkGetAccelerationStructureMemoryRequirementsNV(
    sDevice, &memoryRequirementsInfo, &memoryRequirements);

  VmaAllocationCreateInfo allocationCI = {};
  allocationCI.flags = VMA_MEMORY_USAGE_GPU_ONLY;
  allocationCI.memoryTypeBits =
    memoryRequirements.memoryRequirements.memoryTypeBits;

  if (auto result =
        vmaAllocateMemory(sAllocator, &memoryRequirements.memoryRequirements,
                          &allocationCI, &structure.allocation, nullptr);
      result != VK_SUCCESS) {
    vkDestroyAccelerationStructureNV(sDevice, structure.structure, nullptr);
    return tl::unexpected(std::system_error(iris::make_error_code(result),
                                            "Cannot allocate memory"));
  }

  VmaAllocationInfo allocationInfo;
  vmaGetAllocationInfo(sAllocator, structure.allocation, &allocationInfo);

  VkBindAccelerationStructureMemoryInfoNV bindAccelerationStructureMemoryInfo =
    {};
  bindAccelerationStructureMemoryInfo.sType =
    VK_STRUCTURE_TYPE_BIND_ACCELERATION_STRUCTURE_MEMORY_INFO_NV;
  bindAccelerationStructureMemoryInfo.accelerationStructure =
    structure.structure;
  bindAccelerationStructureMemoryInfo.memory = allocationInfo.deviceMemory;
  bindAccelerationStructureMemoryInfo.memoryOffset = 0;

  if (auto result = vkBindAccelerationStructureMemoryNV(
        sDevice, 1, &bindAccelerationStructureMemoryInfo);
      result != VK_SUCCESS) {
    vmaFreeMemory(sAllocator, structure.allocation);
    vkDestroyAccelerationStructureNV(sDevice, structure.structure, nullptr);
    return tl::unexpected(std::system_error(
      make_error_code(result), "Cannot bind memory to acceleration structure"));
  }

  Ensures(structure.structure != VK_NULL_HANDLE);
  Ensures(structure.allocation != VK_NULL_HANDLE);

  IRIS_LOG_LEAVE();
  return structure;
} // iris::Renderer::CreateAccelerationStructure

tl::expected<iris::Renderer::Pipeline, std::system_error>
iris::Renderer::CreateRasterizationPipeline(
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
  Expects(sDevice != VK_NULL_HANDLE);
  Expects(sRenderPass != VK_NULL_HANDLE);
  Expects(sGlobalDescriptorSetLayout != VK_NULL_HANDLE);

  Pipeline pipeline;

  absl::InlinedVector<VkDescriptorSetLayout, 8> allDescriptorSetLayouts;
  allDescriptorSetLayouts.push_back(sGlobalDescriptorSetLayout);
  std::copy_n(std::begin(descriptorSetLayouts), std::size(descriptorSetLayouts),
              std::back_inserter(allDescriptorSetLayouts));

  VkPushConstantRange pushConstantRange = {};
  pushConstantRange.stageFlags =
    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  pushConstantRange.offset = 0;
  pushConstantRange.size = sizeof(PushConstants);

  VkPipelineLayoutCreateInfo pipelineLayoutCI = {};
  pipelineLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutCI.setLayoutCount =
    gsl::narrow_cast<std::uint32_t>(allDescriptorSetLayouts.size());
  pipelineLayoutCI.pSetLayouts = allDescriptorSetLayouts.data();
  pipelineLayoutCI.pushConstantRangeCount = 1;
  pipelineLayoutCI.pPushConstantRanges = &pushConstantRange;

  if (auto result = vkCreatePipelineLayout(sDevice, &pipelineLayoutCI, nullptr,
                                           &pipeline.layout);
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
  graphicsPipelineCI.renderPass = sRenderPass;
  graphicsPipelineCI.subpass = renderPassSubpass;

  if (auto result = vkCreateGraphicsPipelines(sDevice, VK_NULL_HANDLE, 1,
                                              &graphicsPipelineCI, nullptr,
                                              &pipeline.pipeline);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    vkDestroyPipelineLayout(sDevice, pipeline.layout, nullptr);
    return tl::unexpected(std::system_error(make_error_code(result),
                                            "Cannot create graphics pipeline"));
  }

  Ensures(pipeline.layout != VK_NULL_HANDLE);
  Ensures(pipeline.pipeline != VK_NULL_HANDLE);

  IRIS_LOG_LEAVE();
  return pipeline;
} // iris::Renderer::CreateRasterizationPipeline

tl::expected<iris::Renderer::Pipeline, std::system_error>
iris::Renderer::CreateRayTracingPipeline(
  gsl::span<const Shader> shaders, gsl::span<const ShaderGroup> groups,
  gsl::span<const VkDescriptorSetLayout> descriptorSetLayouts,
  std::uint32_t maxRecursionDepth) noexcept {
  IRIS_LOG_ENTER();
  Expects(sDevice != VK_NULL_HANDLE);
  Expects(sGlobalDescriptorSetLayout != VK_NULL_HANDLE);

  Pipeline pipeline;

  absl::InlinedVector<VkDescriptorSetLayout, 8> allDescriptorSetLayouts;
  allDescriptorSetLayouts.push_back(sGlobalDescriptorSetLayout);
  std::copy_n(std::begin(descriptorSetLayouts), std::size(descriptorSetLayouts),
              std::back_inserter(allDescriptorSetLayouts));

  VkPushConstantRange pushConstantRange = {};
  pushConstantRange.stageFlags =
    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  pushConstantRange.offset = 0;
  pushConstantRange.size = sizeof(PushConstants);

  VkPipelineLayoutCreateInfo pipelineLayoutCI = {};
  pipelineLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutCI.setLayoutCount =
    gsl::narrow_cast<std::uint32_t>(allDescriptorSetLayouts.size());
  pipelineLayoutCI.pSetLayouts = allDescriptorSetLayouts.data();
  pipelineLayoutCI.pushConstantRangeCount = 1;
  pipelineLayoutCI.pPushConstantRanges = &pushConstantRange;

  if (auto result = vkCreatePipelineLayout(sDevice, &pipelineLayoutCI, nullptr,
                                           &pipeline.layout);
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
  pipelineCI.stageCount = shaderStageCIs.size();
  pipelineCI.pStages = shaderStageCIs.data();
  pipelineCI.groupCount = shaderGroupCIs.size();
  pipelineCI.pGroups = shaderGroupCIs.data();
  pipelineCI.maxRecursionDepth = maxRecursionDepth;
  pipelineCI.layout = pipeline.layout;

  if (auto result = vkCreateRayTracingPipelinesNV(
        iris::Renderer::sDevice, VK_NULL_HANDLE, 1, &pipelineCI, nullptr,
        &pipeline.pipeline);
      result != VK_SUCCESS) {
    return tl::unexpected(std::system_error(iris::make_error_code(result),
                                            "Cannot create pipeline"));
  }

  Ensures(pipeline.layout != VK_NULL_HANDLE);
  Ensures(pipeline.pipeline != VK_NULL_HANDLE);

  IRIS_LOG_LEAVE();
  return pipeline;
} // iris::Renderer::CreateRayTracingPipeline

