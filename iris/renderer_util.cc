#include "renderer_util.h"
#if PLATFORM_COMPILER_GCC
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#include "absl/container/inlined_vector.h"
#include "enumerate.h"
#include "error.h"
#include "logging.h"
#include "vulkan_util.h"

tl::expected<std::pair<VkPipelineLayout, VkPipeline>, std::system_error>
iris::Renderer::CreateGraphicsPipeline(
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
  std::uint32_t renderPassSubpass, std::string name) noexcept {
  IRIS_LOG_ENTER();
  Expects(sDevice != VK_NULL_HANDLE);
  Expects(sRenderPass != VK_NULL_HANDLE);

  absl::InlinedVector<VkPushConstantRange, 4> allPushConstantRanges;
  allPushConstantRanges.push_back(
    {VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
     sizeof(ShaderToyPushConstants)});

  for (auto&& range : pushConstantRanges) {
    allPushConstantRanges.push_back(range);
  }

  VkPipelineLayout layout{VK_NULL_HANDLE};
  VkPipeline pipeline{VK_NULL_HANDLE};

  VkPipelineLayoutCreateInfo pipelineLayoutCI = {};
  pipelineLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutCI.setLayoutCount =
    gsl::narrow_cast<std::uint32_t>(descriptorSetLayouts.size());
  pipelineLayoutCI.pSetLayouts = descriptorSetLayouts.data();
  pipelineLayoutCI.pushConstantRangeCount =
    gsl::narrow_cast<std::uint32_t>(allPushConstantRanges.size());
  pipelineLayoutCI.pPushConstantRanges = allPushConstantRanges.data();

  if (auto result =
        vkCreatePipelineLayout(sDevice, &pipelineLayoutCI, nullptr, &layout);
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
                     shader.handle,
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
  graphicsPipelineCI.layout = layout;
  graphicsPipelineCI.renderPass = sRenderPass;
  graphicsPipelineCI.subpass = renderPassSubpass;

  if (auto result = vkCreateGraphicsPipelines(
        sDevice, VK_NULL_HANDLE, 1, &graphicsPipelineCI, nullptr, &pipeline);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(make_error_code(result),
                                            "Cannot create graphics pipeline"));
  }

  if (!name.empty()) {
    NameObject(sDevice, VK_OBJECT_TYPE_PIPELINE_LAYOUT, layout,
               (name + ".layout").c_str());
    NameObject(sDevice, VK_OBJECT_TYPE_PIPELINE, pipeline, name.c_str());
  }

  Ensures(layout != VK_NULL_HANDLE);
  Ensures(pipeline != VK_NULL_HANDLE);
  IRIS_LOG_LEAVE();
  return std::make_pair(layout, pipeline);
} // iris::Renderer::CreateGraphicsPipeline

tl::expected<std::tuple<VkImage, VmaAllocation>, std::system_error>
iris::Renderer::CreateImage(VkFormat format, VkExtent2D extent,
                            VkImageUsageFlags imageUsage,
                            VmaMemoryUsage memoryUsage,
                            gsl::not_null<std::byte*> pixels [[maybe_unused]],
                            std::uint32_t bytesPerPixel) noexcept {
  IRIS_LOG_ENTER();
  Expects(sDevice != VK_NULL_HANDLE);
  Expects(sAllocator != VK_NULL_HANDLE);

  VkDeviceSize imageSize [[maybe_unused]];

  switch (format) {
  case VK_FORMAT_R8G8B8A8_UNORM:
    Expects(bytesPerPixel == sizeof(char) * 4);
    imageSize = extent.width * extent.height * sizeof(char) * 4;
    break;

  case VK_FORMAT_R32_SFLOAT:
    Expects(bytesPerPixel == sizeof(float));
    imageSize = extent.width * extent.height * sizeof(float);
    break;

  default:
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(std::make_error_code(std::errc::invalid_argument),
                        "Unsupported texture format"));
  }

  VkBuffer stagingBuffer = VK_NULL_HANDLE;
  VmaAllocation stagingBufferAllocation = VK_NULL_HANDLE;
  VkDeviceSize stagingBufferSize = 0;

  if (auto bas = CreateOrResizeBuffer(
        sAllocator, stagingBuffer, stagingBufferAllocation, stagingBufferSize,
        imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VMA_MEMORY_USAGE_CPU_TO_GPU)) {
    std::tie(stagingBuffer, stagingBufferAllocation, stagingBufferSize) = *bas;
  } else {
    using namespace std::string_literals;
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(bas.error().code(), "Cannot create staging buffer: "s +
                                              bas.error().what()));
  }

  if (auto p = MapMemory<unsigned char*>(sAllocator, stagingBufferAllocation)) {
    std::memcpy(*p, pixels, imageSize);
  } else {
    using namespace std::string_literals;
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(
      p.error().code(), "Cannot map staging buffer: "s + p.error().what()));
  }

  vmaUnmapMemory(sAllocator, stagingBufferAllocation);

  VkImageCreateInfo imageCI = {};
  imageCI.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageCI.imageType = VK_IMAGE_TYPE_2D;
  imageCI.format = format;
  imageCI.extent = {extent.width, extent.height, 1};
  imageCI.mipLevels = 1;
  imageCI.arrayLayers = 1;
  imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
  imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageCI.usage = imageUsage | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  imageCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  imageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

  VmaAllocationCreateInfo allocationCI = {};
  allocationCI.usage = memoryUsage;

  VkImage image;
  VmaAllocation allocation;

  if (auto result = vmaCreateImage(sAllocator, &imageCI, &allocationCI, &image,
                                   &allocation, nullptr);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    tl::unexpected(
      std::system_error(make_error_code(result), "Cannot create image"));
  }

  // TODO: figure this out
  // TransitionImage
  /*
    TransitionImage(VkDevice device, VkCommandPool commandPool, VkQueue queue,
                    VkFence fence, VkImage image, VkImageLayout oldLayout,
                    VkImageLayout newLayout, std::uint32_t mipLevels,
                    std::uint32_t arrayLayers) noexcept;
  */

  IRIS_LOG_LEAVE();
  return tl::unexpected(std::system_error(Error::kNotImplemented));

#if 0
  if (auto error = image.Transition(VK_IMAGE_LAYOUT_UNDEFINED,
                                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, 1,
                                    commandPool);
      error.code()) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(error);
  }

  VkCommandBuffer commandBuffer;
  if (auto cb = BeginOneTimeSubmit(commandPool)) {
    commandBuffer = *cb;
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(cb.error());
  }

  VkBufferImageCopy region = {};
  region.bufferOffset = 0;
  region.bufferRowLength = 0;
  region.bufferImageHeight = 0;
  region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  region.imageOffset = {0, 0, 0};
  region.imageExtent = extent;

  vkCmdCopyBufferToImage(commandBuffer, stagingBuffer.handle, image.handle,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

  if (auto error = EndOneTimeSubmit(commandBuffer, commandPool);
      error.code()) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(error);
  }

  if (auto error =
        image.Transition(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         (memoryUsage == VMA_MEMORY_USAGE_GPU_ONLY
                            ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                            : VK_IMAGE_LAYOUT_GENERAL),
                         1, 1, commandPool);
      error.code()) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(error);
  }

  IRIS_LOG_LEAVE();
  return std::make_tuple(image, allocation);
#endif
} // iris::Renderer::CreateImage
