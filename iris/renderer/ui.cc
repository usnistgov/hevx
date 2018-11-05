#include "renderer/ui.h"
#include "absl/container/fixed_array.h"
#include "renderer/buffer.h"
#include "renderer/image.h"
#include "renderer/impl.h"
#include "renderer/shader.h"
#include "error.h"
#include "logging.h"
#include "wsi/input.h"
#include "imgui.h"

namespace iris::Renderer::ui {

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

static VkImage sFontImage{VK_NULL_HANDLE};
static VmaAllocation sFontImageAllocation{VK_NULL_HANDLE};
static VkImageView sFontImageView{VK_NULL_HANDLE};
static VkSampler sFontImageSampler{VK_NULL_HANDLE};
static VkBuffer sVertexBuffer{VK_NULL_HANDLE};
static VmaAllocation sVertexBufferAllocation{VK_NULL_HANDLE};
static VkBuffer sIndexBuffer{VK_NULL_HANDLE};
static VmaAllocation sIndexBufferAllocation{VK_NULL_HANDLE};
static VkDescriptorSetLayout sDescriptorSetLayout{VK_NULL_HANDLE};
static absl::FixedArray<VkDescriptorSet> sDescriptorSets;
static VkPipelineLayout sPipelineLayout{VK_NULL_HANDLE};
static VkPipeline sPipeline{VK_NULL_HANDLE};

} // namespace iris::ui::Renderer

std::error_code iris::Renderer::ui::Initialize() noexcept {
  IRIS_LOG_ENTER();
  VkResult result;

  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();

  io.KeyMap[ImGuiKey_Tab] = wsi::Keys::kTab;
  io.KeyMap[ImGuiKey_LeftArrow] = wsi::Keys::kLeft;
  io.KeyMap[ImGuiKey_RightArrow] = wsi::Keys::kRight;
  io.KeyMap[ImGuiKey_UpArrow] = wsi::Keys::kUp;
  io.KeyMap[ImGuiKey_DownArrow] = wsi::Keys::kDown;
  io.KeyMap[ImGuiKey_PageUp] = wsi::Keys::kPageUp;
  io.KeyMap[ImGuiKey_PageDown] = wsi::Keys::kPageDown;
  io.KeyMap[ImGuiKey_Home] = wsi::Keys::kHome;
  io.KeyMap[ImGuiKey_End] = wsi::Keys::kEnd;
  io.KeyMap[ImGuiKey_Insert] = wsi::Keys::kInsert;
  io.KeyMap[ImGuiKey_Delete] = wsi::Keys::kDelete;
  io.KeyMap[ImGuiKey_Backspace] = wsi::Keys::kBackspace;
  io.KeyMap[ImGuiKey_Space] = wsi::Keys::kSpace;
  io.KeyMap[ImGuiKey_Enter] = wsi::Keys::kEnter;
  io.KeyMap[ImGuiKey_Escape] = wsi::Keys::kEscape;
  io.KeyMap[ImGuiKey_A] = wsi::Keys::kA;
  io.KeyMap[ImGuiKey_C] = wsi::Keys::kC;
  io.KeyMap[ImGuiKey_V] = wsi::Keys::kV;
  io.KeyMap[ImGuiKey_X] = wsi::Keys::kX;
  io.KeyMap[ImGuiKey_Y] = wsi::Keys::kY;
  io.KeyMap[ImGuiKey_Z] = wsi::Keys::kZ;

  unsigned char* pixels;
  int width, height, bytes_per_pixel;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height, &bytes_per_pixel);

  if (auto ti = CreateImageFromMemory(
        VK_IMAGE_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM,
        {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height),
         1},
        VK_IMAGE_USAGE_SAMPLED_BIT, VMA_MEMORY_USAGE_GPU_ONLY, pixels,
        bytes_per_pixel)) {
    std::tie(sFontImage, sFontImageAllocation) = *ti;
  } else {
    IRIS_LOG_LEAVE();
    return ti.error();
  }

  if (auto tv = CreateImageView(sFontImage, VK_FORMAT_R8G8B8A8_UNORM,
                                VK_IMAGE_VIEW_TYPE_2D,
                                {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1})) {
    sFontImageView = std::move(*tv);
  } else {
    IRIS_LOG_LEAVE();
    return tv.error();
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

  if (auto result =
        vkCreateSampler(sDevice, &samplerCI, nullptr, &sFontImageSampler);
      result != VK_SUCCESS) {
    GetLogger()->error("Cannot create sampler for UI font texture: {}",
                       to_string(result));
    IRIS_LOG_LEAVE();
    return make_error_code(result);
  }

  if (auto vb = CreateBuffer(1024 * sizeof(ImDrawVert),
                             VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                             VMA_MEMORY_USAGE_CPU_TO_GPU)) {
    std::tie(sVertexBuffer, sVertexBufferAllocation) = *vb;
  } else {
    IRIS_LOG_LEAVE();
    return vb.error();
  }

  if (auto ib =
        CreateBuffer(1024 * sizeof(ImDrawIdx), VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                     VMA_MEMORY_USAGE_CPU_TO_GPU)) {
    std::tie(sIndexBuffer, sIndexBufferAllocation) = *ib;
  } else {
    IRIS_LOG_LEAVE();
    return ib.error();
  }

  VkShaderModule vertexShader;
  if (auto vs = CreateShaderFromSource(sUIVertexShaderSource,
                                       VK_SHADER_STAGE_VERTEX_BIT)) {
    vertexShader = *vs;
  } else {
    IRIS_LOG_LEAVE();
    return vs.error();
  }

  VkShaderModule fragmentShader;
  if (auto fs = CreateShaderFromSource(sUIFragmentShaderSource,
                                       VK_SHADER_STAGE_FRAGMENT_BIT)) {
    fragmentShader = *fs;
  } else {
    IRIS_LOG_LEAVE();
    return fs.error();
  }

  VkDescriptorSetLayoutBinding descriptorSetLayoutBinding = {};
  descriptorSetLayoutBinding.binding = 0;
  descriptorSetLayoutBinding.descriptorType =
    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  descriptorSetLayoutBinding.descriptorCount = 1;
  descriptorSetLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  descriptorSetLayoutBinding.pImmutableSamplers = &sFontImageSampler;

  if (auto d = CreateDescriptors({descriptorSetLayoutBinding})) {
    std::tie(sDescriptorSetLayout, sDescriptorSets) = *d;
  } else {
    IRIS_LOG_LEAVE();
    return tl::make_unexpected(d.error());
  }

  VkDescriptorImageInfo descriptorImageI = {};
  descriptorImageI.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  descriptorImageI.imageView = sFontImageView;
  descriptorImageI.sampler = sFontImageSampler;

  absl::FixedArray<VkWriteDescriptorSet> writeDescriptorSets(
    sDescriptorSets.size());

  for (std::size_t i = 0; i < writeDescriptorSets.size(); ++i) {
    VkWriteDescriptorSet& writeDS = writeDescriptorSets[i];

    writeDS.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeDS.dstSet = sDescriptorSets[i];
    writeDS.dstBinding = 0;
    writeDS.dstArrayElement = 0;
    writeDS.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writeDS.descriptorCount = 1;
    writeDS.pImageInfo = &descriptorImageI;
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
  pipelineLayoutCI.pSetLayouts = &sDescriptorSetLayout;
  pipelineLayoutCI.pushConstantRangeCount = 1;
  pipelineLayoutCI.pPushConstantRanges = &pushConstantRange;

  result = vkCreatePipelineLayout(sDevice, &pipelineLayoutCI, nullptr,
                                           &sPipelineLayout);
  if (result != VK_SUCCESS) {
    GetLogger()->error("Cannot create pipeline layout: {}", to_string(result));
    IRIS_LOG_LEAVE();
    return make_error_code(result);
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
  graphicsPipelineCI.layout = sPipelineLayout;
  graphicsPipelineCI.renderPass = sRenderPass;
  graphicsPipelineCI.subpass = 0;

  result = vkCreateGraphicsPipelines(sDevice, nullptr, 1, &graphicsPipelineCI,
                                     nullptr, &sPipeline);
  if (result != VK_SUCCESS) {
    vkDestroyPipelineLayout(sDevice, sPipelineLayout, nullptr);
    GetLogger()->error("Cannot create pipeline: {}", to_string(result));
    IRIS_LOG_LEAVE();
    return make_error_code(result);
  }

  vkDestroyShaderModule(sDevice, fragmentShader, nullptr);
  vkDestroyShaderModule(sDevice, vertexShader, nullptr);

  NameObject(VK_OBJECT_TYPE_IMAGE, sFontImage, "ui::sFontImage");
  NameObject(VK_OBJECT_TYPE_SAMPLER, sFontImageSampler,
             "ui::sFontImageSampler");
  NameObject(VK_OBJECT_TYPE_BUFFER, sVertexBuffer, "ui::sVertexBuffer");
  NameObject(VK_OBJECT_TYPE_BUFFER, sIndexBuffer, "ui::sIndexBuffer");
  NameObject(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, sDescriptorSetLayout,
             "ui::sDescriptorSetLayout");
  for (auto&& set : sDescriptorSets) {
    NameObject(VK_OBJECT_TYPE_DESCRIPTOR_SET, set, "ui::sDescriptorSets");
  }
  NameObject(VK_OBJECT_TYPE_PIPELINE_LAYOUT, sPipelineLayout,
             "ui::sPipelineLayout");
  NameObject(VK_OBJECT_TYPE_PIPELINE, sPipeline, "ui::sPipeline");

  return Error::kNone;
} // iris::Renderer::ui::Initialize

tl::expected<VkCommandBuffer, std::error_code>
iris::Renderer::ui::Frame(glm::vec2 const& size) noexcept {
  ImGuiIO& io = ImGui::GetIO();

  io.KeyCtrl = io.KeysDown[wsi::Keys::kLeftControl] ||
               io.KeysDown[wsi::Keys::kRightControl];
  io.KeyShift =
    io.KeysDown[wsi::Keys::kLeftShift] || io.KeysDown[wsi::Keys::kRightShift];
  io.KeyAlt =
    io.KeysDown[wsi::Keys::kLeftAlt] || io.KeysDown[wsi::Keys::kRightAlt];

  ImVec2 mouseSaved = io.MousePos;
  io.MousePos = {-FLT_MAX, -FLT_MAX};

  io.DisplaySize = {size.x, size.y};
  io.DisplayFramebufferScale = {1.f, 1.f};

  ImGui::NewFrame();

  VkCommandBuffer cb;
  if (auto cbs = AllocateCommandBuffers(1, VK_COMMAND_BUFFER_LEVEL_SECONDARY)) {
    cb = (*cbs)[0];
  } else {
    return tl::unexpected(cbs.error());
  }

  return cb;
} // iris::Renderer::ui::Frame

void iris::Renderer::ui::Shutdown() noexcept {
  ImGui::DestroyContext();

  if (sPipeline != VK_NULL_HANDLE) {
    vkDestroyPipeline(sDevice, sPipeline, nullptr);
  }

  if (sPipelineLayout != VK_NULL_HANDLE) {
    vkDestroyPipelineLayout(sDevice, sPipelineLayout, nullptr);
  }

  if (sDescriptorSetLayout != VK_NULL_HANDLE) {
    vkDestroyDescriptorSetLayout(sDevice, sDescriptorSetLayout, nullptr);
  }

  if (sIndexBuffer != VK_NULL_HANDLE &&
      sIndexBufferAllocation != VK_NULL_HANDLE) {
    vmaDestroyBuffer(sAllocator, sIndexBuffer, sIndexBufferAllocation);
  }

  if (sVertexBuffer != VK_NULL_HANDLE &&
      sVertexBufferAllocation != VK_NULL_HANDLE) {
    vmaDestroyBuffer(sAllocator, sVertexBuffer, sVertexBufferAllocation);
  }

  if (sFontImageSampler != VK_NULL_HANDLE) {
    vkDestroySampler(sDevice, sFontImageSampler, nullptr);
  }

  if (sFontImageView != VK_NULL_HANDLE) {
    vkDestroyImageView(sDevice, sFontImageView, nullptr);
  }

  if (sFontImage != VK_NULL_HANDLE && sFontImageAllocation != VK_NULL_HANDLE) {
    vmaDestroyImage(sAllocator, sFontImage, sFontImageAllocation);
  }
} // iris::Renderer::ui::Shutdown

