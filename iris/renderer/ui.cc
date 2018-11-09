#include "renderer/ui.h"
#include "absl/container/fixed_array.h"
#include "glm/gtc/type_ptr.hpp"
#include "renderer/buffer.h"
#include "renderer/image.h"
#include "renderer/impl.h"
#include "renderer/shader.h"
#include "error.h"
#include "logging.h"
#include "wsi/input.h"
#include "imgui.h"
#include <chrono>

namespace iris::Renderer {

static std::string const sUIVertexShaderSource = R"(
#version 450 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;
layout(push_constant) uniform uPushConstant {
  vec2 uScale;
  vec2 uTranslate;
};
layout(location = 0) out vec4 Color;
layout(location = 1) out vec2 UV;
out gl_PerVertex {
  vec4 gl_Position;
};
void main() {
  Color = aColor;
  UV = aUV;
  gl_Position = vec4(aPos * uScale + uTranslate, 0.f, 1.f);
})";

static std::string const sUIFragmentShaderSource = R"(
#version 450 core
layout(set = 0, binding = 0) uniform sampler2D sTexture;
layout(location = 0) in vec4 Color;
layout(location = 1) in vec2 UV;
layout(location = 0) out vec4 fColor;
void main() {
  fColor = Color * texture(sTexture, UV.st);
})";

} // namespace iris::Renderer

tl::expected<iris::Renderer::UI, std::system_error>
iris::Renderer::UI::Create() noexcept {
  IRIS_LOG_ENTER();
  VkResult result;
  UI ui;

  if (auto cbs = AllocateCommandBuffers(2, VK_COMMAND_BUFFER_LEVEL_SECONDARY)) {
    ui.commandBuffers = std::move(*cbs);
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(cbs.error());
  }

  ui.context.reset(ImGui::CreateContext());
  ImGui::SetCurrentContext(ui.context.get());
  ImGui::StyleColorsDark();

  ImGuiIO& io = ImGui::GetIO();

  io.KeyMap[ImGuiKey_Tab] = static_cast<int>(wsi::Keys::kTab);
  io.KeyMap[ImGuiKey_LeftArrow] = static_cast<int>(wsi::Keys::kLeft);
  io.KeyMap[ImGuiKey_RightArrow] = static_cast<int>(wsi::Keys::kRight);
  io.KeyMap[ImGuiKey_UpArrow] = static_cast<int>(wsi::Keys::kUp);
  io.KeyMap[ImGuiKey_DownArrow] = static_cast<int>(wsi::Keys::kDown);
  io.KeyMap[ImGuiKey_PageUp] = static_cast<int>(wsi::Keys::kPageUp);
  io.KeyMap[ImGuiKey_PageDown] = static_cast<int>(wsi::Keys::kPageDown);
  io.KeyMap[ImGuiKey_Home] = static_cast<int>(wsi::Keys::kHome);
  io.KeyMap[ImGuiKey_End] = static_cast<int>(wsi::Keys::kEnd);
  io.KeyMap[ImGuiKey_Insert] = static_cast<int>(wsi::Keys::kInsert);
  io.KeyMap[ImGuiKey_Delete] = static_cast<int>(wsi::Keys::kDelete);
  io.KeyMap[ImGuiKey_Backspace] = static_cast<int>(wsi::Keys::kBackspace);
  io.KeyMap[ImGuiKey_Space] = static_cast<int>(wsi::Keys::kSpace);
  io.KeyMap[ImGuiKey_Enter] = static_cast<int>(wsi::Keys::kEnter);
  io.KeyMap[ImGuiKey_Escape] = static_cast<int>(wsi::Keys::kEscape);
  io.KeyMap[ImGuiKey_A] = static_cast<int>(wsi::Keys::kA);
  io.KeyMap[ImGuiKey_C] = static_cast<int>(wsi::Keys::kC);
  io.KeyMap[ImGuiKey_V] = static_cast<int>(wsi::Keys::kV);
  io.KeyMap[ImGuiKey_X] = static_cast<int>(wsi::Keys::kX);
  io.KeyMap[ImGuiKey_Y] = static_cast<int>(wsi::Keys::kY);
  io.KeyMap[ImGuiKey_Z] = static_cast<int>(wsi::Keys::kZ);

  if (!io.Fonts->AddFontFromFileTTF((std::string(kIRISContentDirectory) +
                                     "/assets/fonts/SourceSansPro-Regular.ttf")
                                      .c_str(),
                                    16.f)) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(Error::kInitializationFailed,
                                            "Cannot load UI font file"));
  }

  unsigned char* pixels;
  int width, height, bytes_per_pixel;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height, &bytes_per_pixel);

  if (auto ti = CreateImageFromMemory(
        VK_IMAGE_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM,
        {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height),
         1},
        VK_IMAGE_USAGE_SAMPLED_BIT, VMA_MEMORY_USAGE_GPU_ONLY, pixels,
        bytes_per_pixel)) {
    std::tie(ui.fontImage, ui.fontImageAllocation) = *ti;
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(ti.error());
  }

  if (auto tv = CreateImageView(ui.fontImage, VK_FORMAT_R8G8B8A8_UNORM,
                                VK_IMAGE_VIEW_TYPE_2D,
                                {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1})) {
    ui.fontImageView = std::move(*tv);
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(tv.error());
  }

  VkSamplerCreateInfo samplerCI = {};
  samplerCI.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerCI.magFilter = VK_FILTER_LINEAR;
  samplerCI.minFilter = VK_FILTER_LINEAR;
  samplerCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  samplerCI.mipLodBias = 0.f;
  samplerCI.anisotropyEnable = VK_FALSE;
  samplerCI.maxAnisotropy = 1;
  samplerCI.compareEnable = VK_FALSE;
  samplerCI.compareOp = VK_COMPARE_OP_ALWAYS;
  samplerCI.minLod = -1000.f;
  samplerCI.maxLod = 1000.f;
  samplerCI.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
  samplerCI.unnormalizedCoordinates = VK_FALSE;

  result = vkCreateSampler(sDevice, &samplerCI, nullptr, &ui.fontImageSampler);
  if (result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(
      make_error_code(result), "Cannot create sampler for UI font texture"));
  }

  if (auto vb = Buffer::Create(
        1024 * sizeof(ImDrawVert), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VMA_MEMORY_USAGE_CPU_TO_GPU, "UI::vertexBuffer")) {
    ui.vertexBuffer = std::move(*vb);
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(vb.error());
  }

  if (auto ib = Buffer::Create(
        1024 * sizeof(ImDrawIdx), VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VMA_MEMORY_USAGE_CPU_TO_GPU, "UI::indexBuffer")) {
    ui.indexBuffer = std::move(*ib);
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(ib.error());
  }

  VkShaderModule vertexShader;
  if (auto vs = CreateShaderFromSource(sUIVertexShaderSource,
                                       VK_SHADER_STAGE_VERTEX_BIT)) {
    vertexShader = *vs;
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(vs.error());
  }

  VkShaderModule fragmentShader;
  if (auto fs = CreateShaderFromSource(sUIFragmentShaderSource,
                                       VK_SHADER_STAGE_FRAGMENT_BIT)) {
    fragmentShader = *fs;
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(fs.error());
  }

  absl::FixedArray<VkDescriptorSetLayoutBinding> descriptorSetLayoutBinding(1);
  descriptorSetLayoutBinding[0] = {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                   1, VK_SHADER_STAGE_FRAGMENT_BIT,
                                   &ui.fontImageSampler};

  if (auto d = CreateDescriptors(descriptorSetLayoutBinding)) {
    std::tie(ui.descriptorSetLayout, ui.descriptorSets) = *d;
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(d.error());
  }

  VkDescriptorImageInfo descriptorImageI = {};
  descriptorImageI.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  descriptorImageI.imageView = ui.fontImageView;
  descriptorImageI.sampler = ui.fontImageSampler;

  absl::FixedArray<VkWriteDescriptorSet> writeDescriptorSets(
    ui.descriptorSets.size());

  for (std::size_t i = 0; i < writeDescriptorSets.size(); ++i) {
    VkWriteDescriptorSet& writeDS = writeDescriptorSets[i];

    writeDS.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeDS.pNext = nullptr;
    writeDS.dstSet = ui.descriptorSets[i];
    writeDS.dstBinding = 0;
    writeDS.dstArrayElement = 0;
    writeDS.descriptorCount = 1;
    writeDS.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writeDS.pImageInfo = &descriptorImageI;
    writeDS.pBufferInfo = nullptr;
    writeDS.pTexelBufferView = nullptr;
  }

  UpdateDescriptorSets(writeDescriptorSets);

  VkPushConstantRange pushConstantRange = {};
  pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
  pushConstantRange.offset = 0;
  pushConstantRange.size = sizeof(glm::vec2) * 2;

  absl::FixedArray<VkPipelineShaderStageCreateInfo> shaderStageCIs(2);
  shaderStageCIs[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                       nullptr,
                       0,
                       VK_SHADER_STAGE_VERTEX_BIT,
                       vertexShader,
                       "main",
                       nullptr};
  shaderStageCIs[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                       nullptr,
                       0,
                       VK_SHADER_STAGE_FRAGMENT_BIT,
                       fragmentShader,
                       "main",
                       nullptr};

  VkVertexInputBindingDescription vertexInputBindingDescription;
  vertexInputBindingDescription.binding = 0;
  vertexInputBindingDescription.stride = sizeof(ImDrawVert);
  vertexInputBindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

  std::vector<VkVertexInputAttributeDescription>
    vertexInputAttributeDescriptions(3);
  vertexInputAttributeDescriptions[0] = {0, 0, VK_FORMAT_R32G32_SFLOAT,
                                         offsetof(ImDrawVert, pos)};
  vertexInputAttributeDescriptions[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT,
                                         offsetof(ImDrawVert, uv)};
  vertexInputAttributeDescriptions[2] = {2, 0, VK_FORMAT_R8G8B8A8_UNORM,
                                         offsetof(ImDrawVert, col)};

  VkPipelineVertexInputStateCreateInfo vertexInputStateCI = {};
  vertexInputStateCI.sType =
    VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertexInputStateCI.vertexBindingDescriptionCount = 1;
  vertexInputStateCI.pVertexBindingDescriptions = &vertexInputBindingDescription;
  vertexInputStateCI.vertexAttributeDescriptionCount =
    static_cast<uint32_t>(vertexInputAttributeDescriptions.size());
  vertexInputStateCI.pVertexAttributeDescriptions =
    vertexInputAttributeDescriptions.data();

  VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI = {};
  inputAssemblyStateCI.sType =
    VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  inputAssemblyStateCI.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  VkPipelineViewportStateCreateInfo viewportStateCI = {};
  viewportStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewportStateCI.viewportCount = 1;
  viewportStateCI.scissorCount = 1;

  VkPipelineRasterizationStateCreateInfo rasterizationStateCI = {};
  rasterizationStateCI.sType =
    VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterizationStateCI.polygonMode = VK_POLYGON_MODE_FILL;
  rasterizationStateCI.cullMode = VK_CULL_MODE_FRONT_BIT;
  rasterizationStateCI.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rasterizationStateCI.lineWidth = 1.f;

  VkPipelineMultisampleStateCreateInfo multisampleStateCI = {};
  multisampleStateCI.sType =
    VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisampleStateCI.rasterizationSamples = sSurfaceSampleCount;
  multisampleStateCI.minSampleShading = 1.f;

  VkPipelineDepthStencilStateCreateInfo depthStencilStateCI = {};
  depthStencilStateCI.sType =
    VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;

  VkPipelineColorBlendAttachmentState colorBlendAttachmentState = {};
  colorBlendAttachmentState.blendEnable = VK_TRUE;
  colorBlendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
  colorBlendAttachmentState.dstColorBlendFactor =
    VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  colorBlendAttachmentState.colorBlendOp = VK_BLEND_OP_ADD;
  colorBlendAttachmentState.srcAlphaBlendFactor =
    VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
  colorBlendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
  colorBlendAttachmentState.alphaBlendOp = VK_BLEND_OP_ADD;
  colorBlendAttachmentState.colorWriteMask =
    VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
    VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

  VkPipelineColorBlendStateCreateInfo colorBlendStateCI = {};
  colorBlendStateCI.sType =
    VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlendStateCI.attachmentCount = 1;
  colorBlendStateCI.pAttachments = &colorBlendAttachmentState;

  std::vector<VkDynamicState> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT,
                                               VK_DYNAMIC_STATE_SCISSOR};

  VkPipelineDynamicStateCreateInfo dynamicStateCI = {};
  dynamicStateCI.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamicStateCI.dynamicStateCount =
    static_cast<uint32_t>(dynamicStates.size());
  dynamicStateCI.pDynamicStates = dynamicStates.data();

  VkPipelineLayoutCreateInfo pipelineLayoutCI = {};
  pipelineLayoutCI.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutCI.setLayoutCount = 1;
  pipelineLayoutCI.pSetLayouts = &ui.descriptorSetLayout;
  pipelineLayoutCI.pushConstantRangeCount = 1;
  pipelineLayoutCI.pPushConstantRanges = &pushConstantRange;

  result = vkCreatePipelineLayout(sDevice, &pipelineLayoutCI, nullptr,
                                           &ui.pipelineLayout);
  if (result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(make_error_code(result),
                                            "Cannot create pipeline layout"));
  }

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
  graphicsPipelineCI.layout = ui.pipelineLayout;
  graphicsPipelineCI.renderPass = sRenderPass;
  graphicsPipelineCI.subpass = 0;

  result = vkCreateGraphicsPipelines(sDevice, nullptr, 1, &graphicsPipelineCI,
                                     nullptr, &ui.pipeline);
  if (result != VK_SUCCESS) {
    vkDestroyPipelineLayout(sDevice, ui.pipelineLayout, nullptr);
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(make_error_code(result), "Cannot create pipeline"));
  }

  vkDestroyShaderModule(sDevice, fragmentShader, nullptr);
  vkDestroyShaderModule(sDevice, vertexShader, nullptr);

  //NameObject(VK_OBJECT_TYPE_IMAGE, ui.fontImage, "ui::fontImage");
  //NameObject(VK_OBJECT_TYPE_SAMPLER, ui.fontImageSampler,
             //"ui::fontImageSampler");
  //NameObject(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, sDescriptorSetLayout,
             //"ui::sDescriptorSetLayout");
  //for (auto&& set : sDescriptorSets) {
    //NameObject(VK_OBJECT_TYPE_DESCRIPTOR_SET, set, "ui::sDescriptorSets");
  //}
  //NameObject(VK_OBJECT_TYPE_PIPELINE_LAYOUT, sPipelineLayout,
             //"ui::sPipelineLayout");
  //NameObject(VK_OBJECT_TYPE_PIPELINE, sPipeline, "ui::sPipeline");

  IRIS_LOG_LEAVE();
  return std::move(ui);
} // iris::Renderer::ui::Initialize

iris::Renderer::UI::UI(UI&& other) noexcept
  : commandBuffers(std::move(other.commandBuffers))
  , commandBufferIndex(other.commandBufferIndex)
  , fontImage(other.fontImage)
  , fontImageAllocation(other.fontImageAllocation)
  , fontImageView(other.fontImageView)
  , fontImageSampler(other.fontImageSampler)
  , vertexBuffer(std::move(other.vertexBuffer))
  , indexBuffer(std::move(other.indexBuffer))
  , descriptorSetLayout(other.descriptorSetLayout)
  , descriptorSets(std::move(other.descriptorSets))
  , pipelineLayout(other.pipelineLayout)
  , pipeline(other.pipeline)
  , context(std::move(other.context)) {
  other.fontImage = VK_NULL_HANDLE;
  other.fontImageAllocation = VK_NULL_HANDLE;
  other.fontImageView = VK_NULL_HANDLE;
  other.fontImageSampler = VK_NULL_HANDLE;
  other.descriptorSetLayout = VK_NULL_HANDLE;
  other.pipelineLayout = VK_NULL_HANDLE;
  other.pipeline = VK_NULL_HANDLE;
} // iris::Renderer::UI::UI

iris::Renderer::UI& iris::Renderer::UI::operator=(UI&& rhs) noexcept {
  if (this == &rhs) return *this;

  commandBuffers = std::move(rhs.commandBuffers);
  commandBufferIndex = rhs.commandBufferIndex;
  fontImage = rhs.fontImage;
  fontImageAllocation = rhs.fontImageAllocation;
  fontImageView = rhs.fontImageView;
  fontImageSampler = rhs.fontImageSampler;
  vertexBuffer = std::move(rhs.vertexBuffer);
  indexBuffer = std::move(rhs.indexBuffer);
  descriptorSetLayout = rhs.descriptorSetLayout;
  descriptorSets = std::move(rhs.descriptorSets);
  pipelineLayout = rhs.pipelineLayout;
  pipeline = rhs.pipeline;
  context = std::move(rhs.context);

  rhs.fontImage = VK_NULL_HANDLE;
  rhs.fontImageAllocation = VK_NULL_HANDLE;
  rhs.fontImageView = VK_NULL_HANDLE;
  rhs.fontImageSampler = VK_NULL_HANDLE;
  rhs.descriptorSetLayout = VK_NULL_HANDLE;
  rhs.pipelineLayout = VK_NULL_HANDLE;
  rhs.pipeline = VK_NULL_HANDLE;

  return *this;
} // iris::Renderer::UI::operator=

iris::Renderer::UI::~UI() noexcept {
  if (!context) return;
  IRIS_LOG_ENTER();

  context.reset();
  vkDestroyPipeline(sDevice, pipeline, nullptr);
  vkDestroyPipelineLayout(sDevice, pipelineLayout, nullptr);
  vkDestroyDescriptorSetLayout(sDevice, descriptorSetLayout, nullptr);
  vkDestroySampler(sDevice, fontImageSampler, nullptr);
  vkDestroyImageView(sDevice, fontImageView, nullptr);
  vmaDestroyImage(sAllocator, fontImage, fontImageAllocation);

  IRIS_LOG_LEAVE();
} // iris::Renderer::UI::~UI

