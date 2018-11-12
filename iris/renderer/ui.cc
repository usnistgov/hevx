#include "renderer/ui.h"
#include "absl/container/fixed_array.h"
#include "glm/gtc/type_ptr.hpp"
#include "renderer/buffer.h"
#include "renderer/image.h"
#include "renderer/impl.h"
#include "renderer/pipeline.h"
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
layout(set = 0, binding = 0) uniform sampler sSampler;
layout(set = 0, binding = 1) uniform texture2D sTexture;
layout(location = 0) in vec4 Color;
layout(location = 1) in vec2 UV;
layout(location = 0) out vec4 fColor;
void main() {
  fColor = Color * texture(sampler2D(sTexture, sSampler), UV.st);
})";

} // namespace iris::Renderer

tl::expected<iris::Renderer::UI, std::system_error>
iris::Renderer::UI::Create() noexcept {
  IRIS_LOG_ENTER();
  Expects(sDevice != VK_NULL_HANDLE);

  UI ui;

  if (auto cbs = CommandBuffers::Allocate(sGraphicsCommandPool, 2,
                                          VK_COMMAND_BUFFER_LEVEL_SECONDARY)) {
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

  if (auto ti = Image::CreateFromMemory(
        VK_IMAGE_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM,
        {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height),
         1},
        VK_IMAGE_USAGE_SAMPLED_BIT, VMA_MEMORY_USAGE_GPU_ONLY,
        gsl::not_null(reinterpret_cast<std::byte*>(pixels)), bytes_per_pixel,
        "UI::fontImage")) {
    ui.fontImage = std::move(*ti);
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(ti.error());
  }

  if (auto tv = ui.fontImage.CreateImageView(
        VK_IMAGE_VIEW_TYPE_2D, {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
        "UI::fontImageView")) {
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

  if (auto s = Sampler::Create(samplerCI, "UI::fontImageSampler")) {
    ui.fontImageSampler = std::move(*s);
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(s.error());
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

  absl::FixedArray<Shader> shaders(2);

  if (auto vs = Shader::CreateFromSource(sUIVertexShaderSource,
                                         VK_SHADER_STAGE_VERTEX_BIT)) {
    shaders[0] = std::move(*vs);
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(vs.error());
  }

  if (auto fs = Shader::CreateFromSource(sUIFragmentShaderSource,
                                         VK_SHADER_STAGE_FRAGMENT_BIT)) {
    shaders[1] = std::move(*fs);
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(fs.error());
  }

  absl::FixedArray<VkDescriptorSetLayoutBinding> descriptorSetLayoutBinding(2);
  descriptorSetLayoutBinding[0] = {0, VK_DESCRIPTOR_TYPE_SAMPLER, 1,
                                   VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
  descriptorSetLayoutBinding[1] = {1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1,
                                   VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};

  if (auto d = DescriptorSets::Create(
        descriptorSetLayoutBinding, kNumDescriptorSets, "ui::descriptorSet")) {
    ui.descriptorSets = std::move(*d);
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(d.error());
  }

  VkDescriptorImageInfo descriptorSamplerI = {};
  descriptorSamplerI.sampler = ui.fontImageSampler;

  absl::FixedArray<VkWriteDescriptorSet> writeDescriptorSets(2);
  writeDescriptorSets[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                            nullptr,
                            ui.descriptorSets.sets[0],
                            0,
                            0,
                            1,
                            VK_DESCRIPTOR_TYPE_SAMPLER,
                            &descriptorSamplerI,
                            nullptr,
                            nullptr};

  VkDescriptorImageInfo descriptorImageI = {};
  descriptorImageI.imageView = ui.fontImageView;
  descriptorImageI.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

  writeDescriptorSets[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                            nullptr,
                            ui.descriptorSets.sets[0],
                            1,
                            0,
                            1,
                            VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                            &descriptorImageI,
                            nullptr,
                            nullptr};

  UpdateDescriptorSets(writeDescriptorSets);

  absl::FixedArray<VkPushConstantRange> pushConstantRanges(1);
  pushConstantRanges[0] = {VK_SHADER_STAGE_VERTEX_BIT, 0,
                           sizeof(glm::vec2) * 2};

  absl::FixedArray<VkVertexInputBindingDescription>
    vertexInputBindingDescriptions(1);
  vertexInputBindingDescriptions[0] = {0, sizeof(ImDrawVert),
                                       VK_VERTEX_INPUT_RATE_VERTEX};

  absl::FixedArray<VkVertexInputAttributeDescription>
    vertexInputAttributeDescriptions(3);
  vertexInputAttributeDescriptions[0] = {0, 0, VK_FORMAT_R32G32_SFLOAT,
                                         offsetof(ImDrawVert, pos)};
  vertexInputAttributeDescriptions[1] = {1, 0, VK_FORMAT_R32G32_SFLOAT,
                                         offsetof(ImDrawVert, uv)};
  vertexInputAttributeDescriptions[2] = {2, 0, VK_FORMAT_R8G8B8A8_UNORM,
                                         offsetof(ImDrawVert, col)};

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

  absl::FixedArray<VkPipelineColorBlendAttachmentState>
    colorBlendAttachmentStates(1);
  colorBlendAttachmentStates[0] = {
    VK_TRUE,                             // blendEnable
    VK_BLEND_FACTOR_SRC_ALPHA,           // srcColorBlendFactor
    VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, // dstColorBlendFactor
    VK_BLEND_OP_ADD,                     // colorBlendOp
    VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, // srcAlphaBlendFactor
    VK_BLEND_FACTOR_ZERO,                // dstAlphaBlendFactor
    VK_BLEND_OP_ADD,                     // alphaBlendOp
    VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT // colorWriteMask
  };

  absl::FixedArray<VkDynamicState> dynamicStates{VK_DYNAMIC_STATE_VIEWPORT,
                                                 VK_DYNAMIC_STATE_SCISSOR};

  if (auto p = Pipeline::CreateGraphics(
        gsl::make_span(&ui.descriptorSets.layout, 1), pushConstantRanges,
        shaders, vertexInputBindingDescriptions,
        vertexInputAttributeDescriptions, inputAssemblyStateCI, viewportStateCI,
        rasterizationStateCI, multisampleStateCI, depthStencilStateCI,
        colorBlendAttachmentStates, dynamicStates, 0, "ui::Pipeline")) {
    ui.pipeline = std::move(*p);
  } else {
    return tl::unexpected(p.error());
  }

  IRIS_LOG_LEAVE();
  return std::move(ui);
} // iris::Renderer::ui::Initialize
