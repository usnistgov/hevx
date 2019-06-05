/*! \file
 * \brief \ref iris::Renderer definition.
 */
#include "config.h"

#include "absl/container/flat_hash_map.h"
#include "absl/container/inlined_vector.h"
#include "enumerate.h"
#include "error.h"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtx/matrix_decompose.hpp"
#include "glm/gtc/type_ptr.hpp"
#include "renderer.h"
#if PLATFORM_COMPILER_GCC
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#endif
#include "glslang/Public/ShaderLang.h"
#if PLATFORM_COMPILER_GCC
#pragma GCC diagnostic pop
#endif
#include "gsl/gsl"
#include "io/gltf.h"
#include "io/json.h"
#include "io/shadertoy.h"
#include "pipeline.h"
#include "protos.h"
#include "renderer_private.h"
#include "shader.h"
#include "spdlog/spdlog.h"
#include "tbb/concurrent_queue.h"
#include "tbb/task.h"
#include "tbb/task_scheduler_init.h"
#include "trackball.h"
#include "vulkan.h"
#include "vulkan_util.h"
#include "window.h"
#include "wsi/input.h"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

using namespace std::string_literals;

/////
//
// The logging must be directly defined here instead of including "logging.h".
// This is because we need to define the static logger instance in this file.
//
/////
namespace iris {

static spdlog::logger*
GetLogger(spdlog::sinks_init_list logSinks = {}) noexcept {
  static std::shared_ptr<spdlog::logger> sLogger;
  if (!sLogger) {
    sLogger = std::make_shared<spdlog::logger>("iris", logSinks);
    sLogger->set_level(spdlog::level::trace);
    spdlog::register_logger(sLogger);
    spdlog::set_pattern("[%Y-%m-%d %T.%e] [%t] [%n] %^[%l] %v%$");
  }

  return sLogger.get();
}

} // namespace iris

//! \brief Logs entry into a function.
#define IRIS_LOG_ENTER()                                                       \
  do {                                                                         \
    ::iris::GetLogger()->trace("ENTER: {} ({}:{})", __func__, __FILE__,        \
                               __LINE__);                                      \
    ::iris::GetLogger()->flush();                                              \
  } while (false)

//! \brief Logs leave from a function.
#define IRIS_LOG_LEAVE()                                                       \
  do {                                                                         \
    ::iris::GetLogger()->trace("LEAVE: {} ({}:{})", __func__, __FILE__,        \
                               __LINE__);                                      \
    ::iris::GetLogger()->flush();                                              \
  } while (false)

namespace iris::Renderer {

VkInstance sInstance{VK_NULL_HANDLE};
VkDebugUtilsMessengerEXT sDebugUtilsMessenger{VK_NULL_HANDLE};
VkPhysicalDevice sPhysicalDevice{VK_NULL_HANDLE};
VkDevice sDevice{VK_NULL_HANDLE};
VmaAllocator sAllocator{VK_NULL_HANDLE};
VkRenderPass sRenderPass{VK_NULL_HANDLE};

VkSurfaceFormatKHR const sSurfaceColorFormat{VK_FORMAT_B8G8R8A8_UNORM,
                                             VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
VkFormat const sSurfaceDepthStencilFormat{VK_FORMAT_D32_SFLOAT};
VkSampleCountFlagBits const sSurfaceSampleCount{VK_SAMPLE_COUNT_4_BIT};
VkPresentModeKHR const sSurfacePresentMode{VK_PRESENT_MODE_FIFO_KHR};

VkDescriptorPool sDescriptorPool{VK_NULL_HANDLE};
VkDescriptorSetLayout sGlobalDescriptorSetLayout{VK_NULL_HANDLE};
static VkDescriptorSet sGlobalDescriptorSet{VK_NULL_HANDLE};

VkQueryPool sTimestampsQueryPool{VK_NULL_HANDLE};

absl::flat_hash_map<std::string, iris::Window>& Windows() {
  static absl::flat_hash_map<std::string, iris::Window> sWindows;
  return sWindows;
} // Windows

std::uint32_t sQueueFamilyIndex{UINT32_MAX};
absl::InlinedVector<VkQueue, 16> sCommandQueues;
absl::InlinedVector<VkCommandPool, 16> sCommandPools;
absl::InlinedVector<VkFence, 16> sCommandFences;

static std::uint32_t sCommandQueueHead{1};
static std::uint32_t sCommandQueueFree{UINT32_MAX};
static std::timed_mutex sCommandQueueMutex;

std::uint32_t const sNumRenderPassAttachments{4};
std::uint32_t const sColorTargetAttachmentIndex{0};
std::uint32_t const sColorResolveAttachmentIndex{1};
std::uint32_t const sDepthStencilTargetAttachmentIndex{2};
std::uint32_t const sDepthStencilResolveAttachmentIndex{3};

static Features sFeatures{Features::kNone};
static bool sRunning{false};
static bool sInFrame{false};
static constexpr std::uint32_t const sNumFramesBuffered{2};
static std::uint32_t sFrameNum{0};
static std::chrono::steady_clock::time_point sPreviousFrameTime{};

struct Renderables {
  RenderableID Insert(Component::Renderable renderable) {
    std::lock_guard<decltype(mutex)> lck(mutex);
    auto const newID = nextID++;
    renderables.emplace(newID, std::move(renderable));
    return RenderableID(newID);
  }

  std::optional<Component::Renderable> Remove(RenderableID const& id) {
    std::lock_guard<decltype(mutex)> lck(mutex);
    if (auto pos = renderables.find(id); pos == renderables.end()) {
      return {};
    } else {
      auto old = pos->second;
      renderables.erase(pos);
      return old;
    }
  }

  std::mutex mutex{};
  RenderableID::id_type nextID{0};
  absl::flat_hash_map<RenderableID, Component::Renderable> renderables;
}; // struct Renderables

static Renderables sRenderables{};

// TODO: move this
static Trackball sTrackball;

static glm::vec4 sWorldBoundingSphere{0.f, 0.f, 0.f, 0.f};
static glm::mat4 sWorldMatrix{1.f};
static glm::mat4 sViewMatrix{1.f};

namespace Nav {

static float sResponse{1.f};
static float sScale{1.f};
static glm::vec3 sPosition{0.f, 0.f, 0.f};
static glm::quat sOrientation{1.f, glm::vec3{0.f, 0.f, 0.f}};

/////
//
// Publicly declared API
//
/////

float Response() noexcept {
  return sResponse;
} // Response

void SetResponse(float response) noexcept {
  sResponse = response;
} // SetResponse

float Scale() noexcept {
  return sScale;
} // Scale

void Rescale(float scale) noexcept {
  sScale = scale;
} // Rescale

glm::vec3 Position() noexcept {
  return sPosition;
} // Position

void Reposition(glm::vec3 position) noexcept {
  sPosition = std::move(position);
} // Reposition

glm::quat Orientation() noexcept {
  return sOrientation;
} // Orientation

void Reorient(glm::quat orientation) noexcept {
  sOrientation = std::move(orientation);
} // Reorient

void Pivot(glm::quat const& pivot) noexcept {
  sOrientation = glm::normalize(sOrientation * glm::normalize(pivot));
} // Pivot

glm::mat4 Matrix() noexcept {
  // Uniform scaling commutes with rotation in this case.
  return glm::translate(
    glm::scale(glm::mat4_cast(sOrientation), glm::vec3(sScale, sScale, sScale)),
    sPosition);
#if 0
  glm::mat4 matrix(1.f);
  matrix = glm::scale(matrix, glm::vec3(sScale, sScale, sScale));
  matrix *= glm::mat4_cast(sOrientation);
  matrix = glm::translate(matrix, sPosition);
  return matrix;
#endif
} // Matrix

void Reset() noexcept {
  sScale = 1.f;
  sPosition = glm::vec3(0.f, 0.f, 0.f);
  sOrientation = glm::quat(1.f, glm::vec3(0.f, 0.f, 0.f));
} // Reset

} // namespace Nav

static Buffer sMatricesBuffer;
static Buffer sLightsBuffer;

static absl::FixedArray<VkFence> sFrameFinishedFences(sNumFramesBuffered);

static VkSemaphore sImagesReadyForPresent{VK_NULL_HANDLE};
static std::uint32_t sFrameIndex{0};
static absl::InlinedVector<VkCommandBuffer, 128> sOldCommandBuffers;

static tbb::task_scheduler_init sTaskSchedulerInit{
  tbb::task_scheduler_init::deferred};
static tbb::concurrent_queue<std::function<std::system_error(void)>>
  sIOContinuations{};

static char const* sImageBlitVertexShaderSource = R"(#version 460 core
layout(location = 0) out vec2 UV;
void main() {
    UV = vec2((gl_VertexIndex << 1) & 2, (gl_VertexIndex & 2));
    gl_Position = vec4(UV * 2.0 - 1.0, 0.f, 1.0);
})";

static char const* sImageBlitFragmentShaderSource = R"(#version 460 core
layout(location = 0) in vec2 UV;
layout(location = 0) out vec4 Color;
layout(set = 1, binding = 0) uniform sampler sSampler;
layout(set = 1, binding = 1) uniform texture2D sTexture;
void main() {
  Color = texture(sampler2D(sTexture, sSampler), UV.st);
})";

static VkSampler sImageBlitSampler;
static Pipeline sImageBlitPipeline;
static VkDescriptorSetLayout sImageBlitDescriptorSetLayout{VK_NULL_HANDLE};
static VkDescriptorSet sImageBlitDescriptorSet{VK_NULL_HANDLE};

static char const* sUIVertexShaderSource = R"(#version 460 core
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

static char const* sUIFragmentShaderSource = R"(#version 460 core
layout(set = 1, binding = 0) uniform sampler sSampler;
layout(set = 1, binding = 1) uniform texture2D sTexture;
layout(location = 0) in vec4 Color;
layout(location = 1) in vec2 UV;
layout(location = 0) out vec4 fColor;
void main() {
  fColor = Color * texture(sampler2D(sTexture, sSampler), UV.st);
})";

static Pipeline sUIPipeline;
static VkDescriptorSetLayout sUIDescriptorSetLayout{VK_NULL_HANDLE};
static VkDescriptorSet sUIDescriptorSet{VK_NULL_HANDLE};

static void
CreateEmplaceWindow(iris::Control::Window const& windowMessage) noexcept {
  IRIS_LOG_ENTER();
  auto const& bg = windowMessage.background_color();

  Window::Options options = Window::Options::kNone;
  if (windowMessage.show_system_decoration()) {
    options |= Window::Options::kDecorated;
  }
  if (windowMessage.is_stereo()) options |= Window::Options::kStereo;
  if (windowMessage.show_ui()) options |= Window::Options::kShowUI;

  if (auto win = CreateWindow(
        windowMessage.name().c_str(),
        wsi::Offset2D{gsl::narrow_cast<std::int16_t>(windowMessage.x()),
                      gsl::narrow_cast<std::int16_t>(windowMessage.y())},
        wsi::Extent2D{gsl::narrow_cast<std::uint16_t>(windowMessage.width()),
                      gsl::narrow_cast<std::uint16_t>(windowMessage.height())},
        {bg.r(), bg.g(), bg.b(), bg.a()}, options, windowMessage.display(),
        sNumFramesBuffered)) {
    Windows().emplace(windowMessage.name(), std::move(*win));
  } else {
    GetLogger()->warn("Creating window failed: {}", win.error().what());
  }

  IRIS_LOG_LEAVE();
} // CreateEmplaceWindow

static VkCommandBuffer BlitImage(VkImageView src, VkViewport* pViewport,
                                 VkRect2D* pScissor,
                                 gsl::span<std::byte> pushConstants) noexcept {
  VkDescriptorImageInfo samplerInfo = {};
  samplerInfo.sampler = sImageBlitSampler;

  VkDescriptorImageInfo textureInfo = {};
  textureInfo.imageView = src;
  textureInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

  absl::FixedArray<VkWriteDescriptorSet> writeDescriptorSets(2);
  writeDescriptorSets[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                            nullptr,
                            sImageBlitDescriptorSet,
                            0,
                            0,
                            1,
                            VK_DESCRIPTOR_TYPE_SAMPLER,
                            &samplerInfo,
                            nullptr,
                            nullptr};
  writeDescriptorSets[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                            nullptr,
                            sImageBlitDescriptorSet,
                            1,
                            0,
                            1,
                            VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                            &textureInfo,
                            nullptr,
                            nullptr};

  vkUpdateDescriptorSets(
    sDevice, gsl::narrow_cast<std::uint32_t>(writeDescriptorSets.size()),
    writeDescriptorSets.data(), 0, nullptr);

  VkCommandBufferAllocateInfo commandBufferAI = {};
  commandBufferAI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  commandBufferAI.commandPool = sCommandPools[0];
  commandBufferAI.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
  commandBufferAI.commandBufferCount = 1;

  VkCommandBuffer commandBuffer;
  if (auto result =
        vkAllocateCommandBuffers(sDevice, &commandBufferAI, &commandBuffer);
      result != VK_SUCCESS) {
    GetLogger()->error("Cannot allocate command buffer: {}",
                       iris::to_string(result));
    return VK_NULL_HANDLE;
  }

  VkCommandBufferInheritanceInfo commandBufferII = {};
  commandBufferII.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
  commandBufferII.renderPass = sRenderPass;
  commandBufferII.subpass = 0;
  commandBufferII.framebuffer = VK_NULL_HANDLE;

  VkCommandBufferBeginInfo commandBufferBI = {};
  commandBufferBI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  commandBufferBI.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT |
                          VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
  commandBufferBI.pInheritanceInfo = &commandBufferII;

  vkBeginCommandBuffer(commandBuffer, &commandBufferBI);

  vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    sImageBlitPipeline.pipeline);

  vkCmdBindDescriptorSets(commandBuffer,                   // commandBuffer
                          VK_PIPELINE_BIND_POINT_GRAPHICS, // pipelineBindPoint
                          sImageBlitPipeline.layout,       // layout
                          0,                               // firstSet
                          1,                               // descriptorSetCount
                          &sGlobalDescriptorSet,           // pDescriptorSets
                          0,                               // dynamicOffsetCount
                          nullptr                          // pDynamicOffsets
  );

  vkCmdPushConstants(commandBuffer, sImageBlitPipeline.layout,
                     VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                     0, gsl::narrow_cast<std::uint32_t>(pushConstants.size()),
                     pushConstants.data());

  vkCmdSetViewport(commandBuffer, 0, 1, pViewport);
  vkCmdSetScissor(commandBuffer, 0, 1, pScissor);

  vkCmdBindDescriptorSets(commandBuffer,                   // commandBuffer
                          VK_PIPELINE_BIND_POINT_GRAPHICS, // pipelineBindPoint
                          sImageBlitPipeline.layout,       // layout
                          1,                               // firstSet
                          1,                               // descriptorSetCount
                          &sImageBlitDescriptorSet,        // pDescriptorSets
                          0,                               // dynamicOffsetCount
                          nullptr                          // pDynamicOffsets
  );

  vkCmdDraw(commandBuffer, 3, 1, 0, 0);

  vkEndCommandBuffer(commandBuffer);
  return commandBuffer;
} // BlitImage

static VkCommandBuffer
RenderRenderable(Component::Renderable const& renderable, VkViewport* pViewport,
                 VkRect2D* pScissor,
                 gsl::span<std::byte> pushConstants) noexcept {
  VkCommandBufferAllocateInfo commandBufferAI = {};
  commandBufferAI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  commandBufferAI.commandPool = sCommandPools[0];
  commandBufferAI.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
  commandBufferAI.commandBufferCount = 1;

  VkCommandBuffer commandBuffer;
  if (auto result =
        vkAllocateCommandBuffers(sDevice, &commandBufferAI, &commandBuffer);
      result != VK_SUCCESS) {
    GetLogger()->error("Cannot allocate command buffer: {}",
                       iris::to_string(result));
    return VK_NULL_HANDLE;
  }

  VkCommandBufferInheritanceInfo commandBufferII = {};
  commandBufferII.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
  commandBufferII.renderPass = sRenderPass;
  commandBufferII.subpass = 0;
  commandBufferII.framebuffer = VK_NULL_HANDLE;

  VkCommandBufferBeginInfo commandBufferBI = {};
  commandBufferBI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  commandBufferBI.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT |
                          VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
  commandBufferBI.pInheritanceInfo = &commandBufferII;

  vkBeginCommandBuffer(commandBuffer, &commandBufferBI);
  vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    renderable.pipeline.pipeline);

  vkCmdBindDescriptorSets(commandBuffer,                   // commandBuffer
                          VK_PIPELINE_BIND_POINT_GRAPHICS, // pipelineBindPoint
                          renderable.pipeline.layout,      // layout
                          0,                               // firstSet
                          1,                               // descriptorSetCount
                          &sGlobalDescriptorSet,           // pDescriptorSets
                          0,                               // dynamicOffsetCount
                          nullptr                          // pDynamicOffsets
  );

  vkCmdPushConstants(commandBuffer, renderable.pipeline.layout,
                     VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                     0, gsl::narrow_cast<std::uint32_t>(pushConstants.size()),
                     pushConstants.data());

  vkCmdSetViewport(commandBuffer, 0, 1, pViewport);
  vkCmdSetScissor(commandBuffer, 0, 1, pScissor);

  if (renderable.descriptorSet != VK_NULL_HANDLE) {
    vkCmdBindDescriptorSets(
      commandBuffer,                   // commandBuffer
      VK_PIPELINE_BIND_POINT_GRAPHICS, // pipelineBindPoint
      renderable.pipeline.layout,      // layout
      1,                               // firstSet
      1,                               // descriptorSetCount
      &renderable.descriptorSet,       // pDescriptorSets
      0,                               // dynamicOffsetCount
      nullptr                          // pDynamicOffsets
    );
  }

  if (renderable.vertexBuffer.buffer != VK_NULL_HANDLE) {
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &renderable.vertexBuffer.buffer,
                           &renderable.vertexBufferBindingOffset);
  }

  if (renderable.indexBuffer.buffer != VK_NULL_HANDLE) {
    vkCmdBindIndexBuffer(commandBuffer, renderable.indexBuffer.buffer,
                         renderable.indexBufferBindingOffset,
                         renderable.indexType);
  }

  if (renderable.numIndices > 0) {
    vkCmdDrawIndexed(commandBuffer, renderable.numIndices,
                     renderable.instanceCount, renderable.firstIndex,
                     renderable.vertexOffset, renderable.firstInstance);
  } else {
    vkCmdDraw(commandBuffer, renderable.numVertices, renderable.instanceCount,
              renderable.firstVertex, renderable.firstInstance);
  }

  vkEndCommandBuffer(commandBuffer);
  return commandBuffer;
} // RenderRenderable

static absl::InlinedVector<VkCommandBuffer, 32> RenderUI(
  VkCommandBuffer commandBuffer, Window& window) {
  ImGui::Render();
  ImDrawData* drawData = ImGui::GetDrawData();
  if (!drawData || drawData->TotalVtxCount == 0) return {};

  if (auto vb = ReallocateBuffer(
        window.uiVertexBuffer, drawData->TotalVtxCount * sizeof(ImDrawVert),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU)) {
    window.uiVertexBuffer = std::move(*vb);
    NameObject(VK_OBJECT_TYPE_BUFFER, window.uiVertexBuffer.buffer,
               (window.title + "::uiVertexBuffer").c_str());
  } else {
    GetLogger()->warn(
      "Unable to create/resize ui vertex buffer for window {}: {}",
      window.title, vb.error().what());
    return {};
  }

  if (auto ib = ReallocateBuffer(
        window.uiIndexBuffer, drawData->TotalIdxCount * sizeof(ImDrawIdx),
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU)) {
    window.uiIndexBuffer = std::move(*ib);
    NameObject(VK_OBJECT_TYPE_BUFFER, window.uiIndexBuffer.buffer,
               (window.title + "::uiIndexBuffer").c_str());
  } else {
    GetLogger()->warn(
      "Unable to create/resize ui index buffer for window {}: {}", window.title,
      ib.error().what());
    return {};
  }

  ImDrawVert* pVertices;
  ImDrawIdx* pIndices;

  if (auto ptr = window.uiVertexBuffer.Map<ImDrawVert*>()) {
    pVertices = std::move(*ptr);
  } else {
    GetLogger()->warn("Unable to map ui vertex buffer for window {}: {}",
                      window.title, ptr.error().what());
    return {};
  }

  if (auto ptr = window.uiIndexBuffer.Map<ImDrawIdx*>()) {
    pIndices = std::move(*ptr);
  } else {
    GetLogger()->warn("Unable to map ui index buffer for window {}: {}",
                      window.title, ptr.error().what());
    return {};
  }

  for (int i = 0; i < drawData->CmdListsCount; ++i) {
    ImDrawList const* cmdList = drawData->CmdLists[i];
    std::memcpy(pVertices, cmdList->VtxBuffer.Data,
                cmdList->VtxBuffer.Size * sizeof(ImDrawVert));
    std::memcpy(pIndices, cmdList->IdxBuffer.Data,
                cmdList->IdxBuffer.Size * sizeof(ImDrawIdx));
    pVertices += cmdList->VtxBuffer.Size;
    pIndices += cmdList->IdxBuffer.Size;
  }

  window.uiVertexBuffer.Unmap();
  window.uiIndexBuffer.Unmap();

  VkDescriptorImageInfo uiSamplerInfo = {};
  uiSamplerInfo.sampler = window.uiFontTextureSampler;

  VkDescriptorImageInfo uiTextureInfo = {};
  uiTextureInfo.imageView = window.uiFontTextureView;
  uiTextureInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

  absl::FixedArray<VkWriteDescriptorSet> uiWriteDescriptorSets(2);
  uiWriteDescriptorSets[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                              nullptr,
                              sUIDescriptorSet,
                              0,
                              0,
                              1,
                              VK_DESCRIPTOR_TYPE_SAMPLER,
                              &uiSamplerInfo,
                              nullptr,
                              nullptr};
  uiWriteDescriptorSets[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                              nullptr,
                              sUIDescriptorSet,
                              1,
                              0,
                              1,
                              VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                              &uiTextureInfo,
                              nullptr,
                              nullptr};

  vkUpdateDescriptorSets(
    sDevice, gsl::narrow_cast<std::uint32_t>(uiWriteDescriptorSets.size()),
    uiWriteDescriptorSets.data(), 0, nullptr);

  glm::vec2 const displaySize = drawData->DisplaySize;
  glm::vec2 const displayPos = drawData->DisplayPos;

  VkViewport viewport = {0.f, 0.f, displaySize.x, displaySize.y, 0.f, 1.f};

  absl::FixedArray<glm::vec2> uiPushConstants(2);
  uiPushConstants[0] = glm::vec2(2.f, 2.f) / displaySize;
  uiPushConstants[1] = glm::vec2(-1.f, -1.f) - displayPos * uiPushConstants[0];

  absl::InlinedVector<VkCommandBuffer, 32> commandBuffers;

  for (int j = 0, idOff = 0, vtOff = 0; j < drawData->CmdListsCount; ++j) {
    ImDrawList* cmdList = drawData->CmdLists[j];

    for (auto&& drawCmd : cmdList->CmdBuffer) {
      VkRect2D scissor;
      scissor.offset.x = (int32_t)(drawCmd.ClipRect.x - displayPos.x) > 0
                           ? (int32_t)(drawCmd.ClipRect.x - displayPos.x)
                           : 0;
      scissor.offset.y = (int32_t)(drawCmd.ClipRect.y - displayPos.y) > 0
                           ? (int32_t)(drawCmd.ClipRect.y - displayPos.y)
                           : 0;
      scissor.extent.width =
        (uint32_t)(drawCmd.ClipRect.z - drawCmd.ClipRect.x);
      scissor.extent.height =
        (uint32_t)(drawCmd.ClipRect.w - drawCmd.ClipRect.y + 1);
      // TODO: why + 1 above?

      Component::Renderable renderable;
      renderable.pipeline = sUIPipeline;
      renderable.descriptorSetLayout = sUIDescriptorSetLayout;
      renderable.descriptorSet = sUIDescriptorSet;
      renderable.textures.push_back(window.uiFontTexture);
      renderable.textureViews.push_back(window.uiFontTextureView);
      renderable.textureSamplers.push_back(window.uiFontTextureSampler);
      renderable.vertexBuffer = window.uiVertexBuffer;
      renderable.indexBuffer = window.uiIndexBuffer;
      renderable.indexType = VK_INDEX_TYPE_UINT16;
      renderable.numIndices = drawCmd.ElemCount;
      renderable.firstIndex = idOff;
      renderable.firstVertex = vtOff;

      VkCommandBuffer cb =
        RenderRenderable(renderable, &viewport, &scissor,
                         gsl::make_span<std::byte>(
                           reinterpret_cast<std::byte*>(uiPushConstants.data()),
                           uiPushConstants.size() * sizeof(glm::vec2)));
      vkCmdExecuteCommands(commandBuffer, 1, &cb);
      commandBuffers.push_back(cb);

      idOff += drawCmd.ElemCount;
    }

    vtOff += cmdList->VtxBuffer.Size;
  }

  return commandBuffers;
} // RenderUI

static VKAPI_ATTR VkBool32 VKAPI_CALL DebugUtilsMessengerCallback(
  VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
  VkDebugUtilsMessageTypeFlagsEXT messageTypes,
  VkDebugUtilsMessengerCallbackDataEXT const* pCallbackData, void*) {
  using namespace std::string_literals;

  // TODO: this is probably not good to rely on
  // Try not to print out object tracker messages
  if (std::strcmp(pCallbackData->pMessageIdName,
        "UNASSIGNED-ObjectTracker-Info") == 0) {
    return VK_FALSE;
  }

  fmt::memory_buffer buf;
  fmt::format_to(buf, "{}: {} ({})",
                 vk::to_string(static_cast<VkDebugUtilsMessageTypeFlagBitsEXT>(
                   messageTypes)),
                 pCallbackData->pMessage, pCallbackData->pMessageIdName);
  std::string const msg(buf.data(), buf.size());

  buf.clear();
  for (uint32_t i = 0; i < pCallbackData->objectCount; ++i) {
    if (pCallbackData->pObjects[i].pObjectName) {
      fmt::format_to(buf, "{}, ", pCallbackData->pObjects[i].pObjectName);
    }
  }
  std::string const objNames(buf.data(), buf.size() == 0 ? 0 : buf.size() - 2);

  if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
    if (objNames.empty()) {
      GetLogger()->error(msg);
    } else {
      GetLogger()->error("{} Objects: ({})", msg, objNames);
    }
  } else if (messageSeverity >=
             VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
    if (objNames.empty()) {
      GetLogger()->warn(msg);
    } else {
      GetLogger()->warn("{} Objects: ({})", msg, objNames);
    }
  } else if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
    if (objNames.empty()) {
      GetLogger()->info(msg);
    } else {
      GetLogger()->info("{} Objects: ({})", msg, objNames);
    }
  } else if (messageSeverity >=
             VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) {
    if (objNames.empty()) {
      GetLogger()->trace(msg);
    } else {
      GetLogger()->trace("{} Objects: ({})", msg, objNames);
    }
  }

  GetLogger()->flush();
  return VK_FALSE;
} // DebugUtilsMessengerCallback

} // namespace iris::Renderer

tl::expected<void, std::system_error>
iris::Renderer::Initialize(gsl::czstring<> appName, Options const& options,
                           spdlog::sinks_init_list logSinks,
                           std::uint32_t appVersion) noexcept {
  GetLogger(logSinks);
  Expects(sInstance == VK_NULL_HANDLE);
  IRIS_LOG_ENTER();

  GOOGLE_PROTOBUF_VERIFY_VERSION;
  glslang::InitializeProcess();

  sTaskSchedulerInit.initialize();
  GetLogger()->debug("Default number of task threads: {}",
                     sTaskSchedulerInit.default_num_threads());

#if PLATFORM_LINUX
  ::setenv(
    "VK_LAYER_PATH",
    (iris::kVulkanSDKDirectory + "/etc/vulkan/explicit_layer.d"s).c_str(), 0);
#elif PLATFORM_WINDOWS
  ::_putenv(("VK_LAYER_PATH="s + iris::kVulkanSDKDirectory + "\\Bin"s).c_str());
#endif
  GetLogger()->debug("VK_LAYER_PATH: {}", ::getenv("VK_LAYER_PATH"));

  flextVkInit();

  std::uint32_t instanceVersion, versionMajor, versionMinor, versionPatch;
  vkEnumerateInstanceVersion(&instanceVersion); // can only return VK_SUCCESS
  versionMajor = VK_VERSION_MAJOR(instanceVersion);
  versionMinor = VK_VERSION_MINOR(instanceVersion);
  versionPatch = VK_VERSION_PATCH(instanceVersion);

  GetLogger()->debug("Vulkan Instance Version: {}.{}.{}", versionMajor,
                     versionMinor, versionPatch);

  if (versionMajor != 1 && versionMinor != 1) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(Error::kInitializationFailed,
                        fmt::format("Invalid instance version: {}.{}.{}",
                                    versionMajor, versionMinor, versionPatch)));
  }

  // Get the number of instance extension properties.
  std::uint32_t numExtensionProperties;
  if (auto result = vkEnumerateInstanceExtensionProperties(
        nullptr, &numExtensionProperties, nullptr);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(make_error_code(result),
                        "Cannot enumerate instance extension properties"));
  }

  // Get the instance extension properties.
  absl::FixedArray<VkExtensionProperties> extensionProperties(
    numExtensionProperties);
  if (auto result = vkEnumerateInstanceExtensionProperties(
        nullptr, &numExtensionProperties, extensionProperties.data());
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(make_error_code(result),
                        "Cannot enumerate instance extension properties"));
  }

  absl::InlinedVector<gsl::czstring<>, 1> layerNames;
  if ((options & Options::kEnableValidation) == Options::kEnableValidation) {
    layerNames.push_back("VK_LAYER_KHRONOS_validation");
  }

  // These are the extensions that we require from the instance.
  absl::InlinedVector<gsl::czstring<>, 10> instanceExtensionNames = {
    VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
    VK_KHR_SURFACE_EXTENSION_NAME, // surfaces are necessary for graphics
    VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME,
#if defined(VK_USE_PLATFORM_XCB_KHR) // plus the platform-specific surface
    VK_KHR_XCB_SURFACE_EXTENSION_NAME,
#elif defined(VK_USE_PLATFORM_WIN32_KHR) // plus the platform-specific surface
    VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#endif
  };

  if ((options & Options::kReportDebugMessages) ==
      Options::kReportDebugMessages) {
    instanceExtensionNames.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  }

  // These are the features that we require from the physical device.
  VkPhysicalDeviceFeatures2 physicalDeviceFeatures = {};
  physicalDeviceFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  physicalDeviceFeatures.features.fullDrawIndexUint32 = VK_TRUE;
  physicalDeviceFeatures.features.geometryShader = VK_TRUE;
  physicalDeviceFeatures.features.tessellationShader = VK_TRUE;
  physicalDeviceFeatures.features.depthClamp = VK_TRUE;
  physicalDeviceFeatures.features.fillModeNonSolid = VK_TRUE;
  physicalDeviceFeatures.features.wideLines = VK_TRUE;
  physicalDeviceFeatures.features.largePoints = VK_TRUE;
  physicalDeviceFeatures.features.multiViewport = VK_TRUE;
  physicalDeviceFeatures.features.pipelineStatisticsQuery = VK_TRUE;
  physicalDeviceFeatures.features.shaderTessellationAndGeometryPointSize =
    VK_TRUE;
  physicalDeviceFeatures.features.shaderUniformBufferArrayDynamicIndexing =
    VK_TRUE;
  physicalDeviceFeatures.features.shaderSampledImageArrayDynamicIndexing =
    VK_TRUE;
  physicalDeviceFeatures.features.shaderStorageBufferArrayDynamicIndexing =
    VK_TRUE;
  physicalDeviceFeatures.features.shaderStorageImageArrayDynamicIndexing =
    VK_TRUE;
  physicalDeviceFeatures.features.shaderClipDistance = VK_TRUE;
  physicalDeviceFeatures.features.shaderCullDistance = VK_TRUE;
  physicalDeviceFeatures.features.shaderFloat64 = VK_TRUE;
  physicalDeviceFeatures.features.shaderInt64 = VK_TRUE;

  // These are the extensions that we require from the physical device.
  absl::InlinedVector<char const*, 32> physicalDeviceExtensionNames{{
    VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME,
    VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME, // core in 1.1, but
                                                     // necessary for
                                                     // DEDICATED_ALLOCATION
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
  }};

  for (auto&& prop : extensionProperties) {
    if (std::strcmp(prop.extensionName, VK_NV_RAY_TRACING_EXTENSION_NAME) ==
        0) {
      physicalDeviceExtensionNames.push_back(VK_NV_RAY_TRACING_EXTENSION_NAME);
      sFeatures |= Features::kRayTracing;
    }
  }

  if (auto instance = vk::CreateInstance(
        appName,
        (appVersion == 0
           ? VK_MAKE_VERSION(kVersionMajor, kVersionMinor, kVersionPatch)
           : appVersion),
        instanceExtensionNames, layerNames,
        (options & Options::kReportDebugMessages) ==
            Options::kReportDebugMessages
          ? &DebugUtilsMessengerCallback
          : nullptr)) {
    sInstance = std::move(*instance);
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(instance.error());
  }

  flextVkInitInstance(sInstance); // initialize instance function pointers
  vk::DumpPhysicalDevices(sInstance);

  if ((options & Options::kReportDebugMessages) ==
      Options::kReportDebugMessages) {
    if (auto messenger = vk::CreateDebugUtilsMessenger(
          sInstance, &DebugUtilsMessengerCallback)) {
      sDebugUtilsMessenger = std::move(*messenger);
    } else {
      GetLogger()->warn("Cannot create DebugUtilsMessenger: {}",
                        messenger.error().what());
    }
  }

  if (auto physicalDevice = vk::ChoosePhysicalDevice(
        sInstance, physicalDeviceFeatures, physicalDeviceExtensionNames,
        VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) {
    sPhysicalDevice = std::move(*physicalDevice);
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(physicalDevice.error());
  }

  if (auto qfi = vk::GetQueueFamilyIndex(
        sPhysicalDevice, VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) {
    sQueueFamilyIndex = *qfi;
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(qfi.error());
  }

  std::uint32_t numQueues;
  if (auto dn =
        vk::CreateDevice(sPhysicalDevice, physicalDeviceFeatures,
                         physicalDeviceExtensionNames, sQueueFamilyIndex)) {
    std::tie(sDevice, numQueues) = *dn;
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(dn.error());
  }

  NameObject(VK_OBJECT_TYPE_INSTANCE, sInstance, "sInstance");
  NameObject(VK_OBJECT_TYPE_PHYSICAL_DEVICE, sPhysicalDevice,
             "sPhysicalDevice");
  NameObject(VK_OBJECT_TYPE_DEVICE, sDevice, "sDevice");

  VkCommandPoolCreateInfo commandPoolCI = {};
  commandPoolCI.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  commandPoolCI.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  commandPoolCI.queueFamilyIndex = sQueueFamilyIndex;

  VkFenceCreateInfo fenceCI = {};
  fenceCI.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

  sCommandQueues.resize(numQueues);
  for (auto&& [i, commandQueue] : enumerate(sCommandQueues)) {
    vkGetDeviceQueue(sDevice, sQueueFamilyIndex,
                     gsl::narrow_cast<std::uint32_t>(i), &commandQueue);
    NameObject(VK_OBJECT_TYPE_QUEUE, commandQueue,
               fmt::format("sCommandQueue[{}]", i).c_str());
  }

  sCommandPools.resize(numQueues);
  for (auto&& [i, commandPool] : enumerate(sCommandPools)) {
    if (auto result = vkCreateCommandPool(sDevice, &commandPoolCI, nullptr,
                                          &sCommandPools[i]);
        result != VK_SUCCESS) {
      IRIS_LOG_LEAVE();
      return tl::unexpected(std::system_error(
        make_error_code(result), "Cannot create graphics command pool"));
    }

    NameObject(VK_OBJECT_TYPE_COMMAND_POOL, commandPool,
               fmt::format("sCommandPools[{}]", i).c_str());
  }

  sCommandFences.resize(numQueues);
  for (auto&& [i, commandFence] : enumerate(sCommandFences)) {
    if (auto result = vkCreateFence(sDevice, &fenceCI, nullptr, &commandFence);
        result != VK_SUCCESS) {
      IRIS_LOG_LEAVE();
      return tl::unexpected(std::system_error(
        make_error_code(result), "Cannot create graphics submit fence"));
    }

    NameObject(VK_OBJECT_TYPE_FENCE, commandFence,
               fmt::format("sCommandFences[{}]", i).c_str());
  }

  if (auto allocator = vk::CreateAllocator(sPhysicalDevice, sDevice)) {
    sAllocator = std::move(*allocator);
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(allocator.error());
  }

  /////
  //
  // Create the RenderPass
  //
  ////

  absl::FixedArray<VkAttachmentDescription> attachments(
    sNumRenderPassAttachments);
  attachments[sColorTargetAttachmentIndex] = VkAttachmentDescription{
    0,                                       // flags
    sSurfaceColorFormat.format,              // format
    sSurfaceSampleCount,                     // samples
    VK_ATTACHMENT_LOAD_OP_CLEAR,             // loadOp (color and depth)
    VK_ATTACHMENT_STORE_OP_DONT_CARE,        // storeOp (color and depth)
    VK_ATTACHMENT_LOAD_OP_DONT_CARE,         // stencilLoadOp
    VK_ATTACHMENT_STORE_OP_DONT_CARE,        // stencilStoreOp
    VK_IMAGE_LAYOUT_UNDEFINED,               // initialLayout
    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL // finalLayout
  };

  attachments[sColorResolveAttachmentIndex] = VkAttachmentDescription{
    0,                                // flags
    sSurfaceColorFormat.format,       // format
    VK_SAMPLE_COUNT_1_BIT,            // samples
    VK_ATTACHMENT_LOAD_OP_DONT_CARE,  // loadOp (color and depth)
    VK_ATTACHMENT_STORE_OP_STORE,     // storeOp (color and depth)
    VK_ATTACHMENT_LOAD_OP_DONT_CARE,  // stencilLoadOp
    VK_ATTACHMENT_STORE_OP_DONT_CARE, // stencilStoreOp
    VK_IMAGE_LAYOUT_UNDEFINED,        // initialLayout
    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR   // finalLayout
  };

  attachments[sDepthStencilTargetAttachmentIndex] = VkAttachmentDescription{
    0,                                // flags
    sSurfaceDepthStencilFormat,       // format
    sSurfaceSampleCount,              // samples
    VK_ATTACHMENT_LOAD_OP_CLEAR,      // loadOp (color and depth)
    VK_ATTACHMENT_STORE_OP_DONT_CARE, // storeOp (color and depth)
    VK_ATTACHMENT_LOAD_OP_CLEAR,      // stencilLoadOp
    VK_ATTACHMENT_STORE_OP_DONT_CARE, // stencilStoreOp
    VK_IMAGE_LAYOUT_UNDEFINED,        // initialLayout
    VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL // finalLayout
  };

  attachments[sDepthStencilResolveAttachmentIndex] = VkAttachmentDescription{
    0,                               // flags
    sSurfaceDepthStencilFormat,      // format
    VK_SAMPLE_COUNT_1_BIT,           // samples
    VK_ATTACHMENT_LOAD_OP_DONT_CARE, // loadOp (color and depth)
    VK_ATTACHMENT_STORE_OP_STORE,    // storeOp (color and depth)
    VK_ATTACHMENT_LOAD_OP_DONT_CARE, // stencilLoadOp
    VK_ATTACHMENT_STORE_OP_STORE,    // stencilStoreOp
    VK_IMAGE_LAYOUT_UNDEFINED,       // initialLayout
    VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL // finalLayout
  };

  VkAttachmentReference color{sColorTargetAttachmentIndex,
                              VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkAttachmentReference resolve{sColorResolveAttachmentIndex,
                                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkAttachmentReference depthStencil{
    sDepthStencilTargetAttachmentIndex,
    VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

  VkSubpassDescription subpass = {
    0,                               // flags
    VK_PIPELINE_BIND_POINT_GRAPHICS, // pipelineBindPoint
    0,                               // inputAttachmentCount
    nullptr,                         // pInputAttachments
    1,                               // colorAttachmentCount
    &color,                          // pColorAttachments (array)
    &resolve,                        // pResolveAttachments (array)
    &depthStencil,                   // pDepthStencilAttachment (single)
    0,                               // preserveAttachmentCount
    nullptr                          // pPreserveAttachments
  };

  VkRenderPassCreateInfo renderPassCI = {};
  renderPassCI.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  renderPassCI.attachmentCount =
    gsl::narrow_cast<std::uint32_t>(attachments.size());
  renderPassCI.pAttachments = attachments.data();
  renderPassCI.subpassCount = 1;
  renderPassCI.pSubpasses = &subpass;

  if (auto result =
        vkCreateRenderPass(sDevice, &renderPassCI, nullptr, &sRenderPass);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(make_error_code(result), "Cannot create render pass"));
  }

  NameObject(VK_OBJECT_TYPE_RENDER_PASS, sRenderPass, "sRenderPass");

  for (auto&& fence : sFrameFinishedFences) {
    if (auto result = vkCreateFence(sDevice, &fenceCI, nullptr, &fence);
        result != VK_SUCCESS) {
      IRIS_LOG_LEAVE();
      return tl::unexpected(std::system_error(
        make_error_code(result), "Cannot create frame finished fence"));
    }
  }

  VkSemaphoreCreateInfo semaphoreCI = {};
  semaphoreCI.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

  if (auto result = vkCreateSemaphore(sDevice, &semaphoreCI, nullptr,
                                      &sImagesReadyForPresent);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(
      make_error_code(result), "Cannot create images ready semaphore"));
  }

  absl::FixedArray<VkDescriptorPoolSize> poolSizes{
    {VK_DESCRIPTOR_TYPE_SAMPLER, 128},
    {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 128},
    {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 128},
    {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 128},
    {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 128},
    {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 128},
    {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 128},
    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 128},
    {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 128},
    {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 128},
    {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 128},
    {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV, 128},
  };

  VkDescriptorPoolCreateInfo descriptorPoolCI = {};
  descriptorPoolCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  descriptorPoolCI.maxSets = 128;
  descriptorPoolCI.poolSizeCount =
    gsl::narrow_cast<std::uint32_t>(poolSizes.size());
  descriptorPoolCI.pPoolSizes = poolSizes.data();

  if (auto result = vkCreateDescriptorPool(sDevice, &descriptorPoolCI, nullptr,
                                           &sDescriptorPool);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(make_error_code(result),
                                            "Cannot create descriptor pool"));
  }

  NameObject(VK_OBJECT_TYPE_DESCRIPTOR_POOL, sDescriptorPool,
             "sDescriptorPool");

  VkQueryPoolCreateInfo queryPoolCI = {};
  queryPoolCI.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
  queryPoolCI.queryType = VK_QUERY_TYPE_TIMESTAMP;
  queryPoolCI.queryCount = 128;

  if (auto result = vkCreateQueryPool(sDevice, &queryPoolCI, nullptr,
                                      &sTimestampsQueryPool);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(
      make_error_code(result), "Cannot create timestamps query pool"));
  }

  /////
  //
  // Create the global descriptor set for all pipelines
  //
  /////

  absl::FixedArray<VkDescriptorSetLayoutBinding> descriptorSetLayoutBindings{
    // This is the MatricesBuffer
    {
      0,                                 // binding
      VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, // descriptorType
      1,                                 // descriptorCount
      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT |
        VK_SHADER_STAGE_RAYGEN_BIT_NV |
        VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV, // stageFlags
      nullptr                               // pImmutableSamplers
    },
    // This is the LightsBuffer
    {
      1,                                 // binding
      VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, // descriptorType
      1,                                 // descriptorCount
      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT |
        VK_SHADER_STAGE_RAYGEN_BIT_NV |
        VK_SHADER_STAGE_CLOSEST_HIT_BIT_NV, // stageFlags
      nullptr                               // pImmutableSamplers
    }};

  VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCI = {};
  descriptorSetLayoutCI.sType =
    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  descriptorSetLayoutCI.bindingCount =
    gsl::narrow_cast<std::uint32_t>(descriptorSetLayoutBindings.size());
  descriptorSetLayoutCI.pBindings = descriptorSetLayoutBindings.data();

  if (auto result = vkCreateDescriptorSetLayout(
        sDevice, &descriptorSetLayoutCI, nullptr, &sGlobalDescriptorSetLayout);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(
      make_error_code(result), "Cannot create descriptor set layout"));
  }

  NameObject(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, sGlobalDescriptorSetLayout,
             "sGlobalDescriptorSetLayout");

  VkDescriptorSetAllocateInfo descriptorSetAI = {};
  descriptorSetAI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  descriptorSetAI.descriptorPool = sDescriptorPool;
  descriptorSetAI.descriptorSetCount = 1;
  descriptorSetAI.pSetLayouts = &sGlobalDescriptorSetLayout;

  if (auto result = vkAllocateDescriptorSets(sDevice, &descriptorSetAI,
                                             &sGlobalDescriptorSet);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(make_error_code(result),
                                            "Cannot allocate descriptor set"));
  }

  NameObject(VK_OBJECT_TYPE_DESCRIPTOR_SET, sGlobalDescriptorSet,
             "sGlobalDescriptorSet");

  if (auto buf = AllocateBuffer(sizeof(MatricesBuffer),
                                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                                VMA_MEMORY_USAGE_CPU_TO_GPU)) {
    sMatricesBuffer = std::move(*buf);
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(buf.error());
  }

  NameObject(VK_OBJECT_TYPE_BUFFER, sMatricesBuffer.buffer, "sMatricesBuffer");

  if (auto buf =
        AllocateBuffer(sizeof(LightsBuffer), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                       VMA_MEMORY_USAGE_CPU_TO_GPU)) {
    sLightsBuffer = std::move(*buf);
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(buf.error());
  }

  NameObject(VK_OBJECT_TYPE_BUFFER, sLightsBuffer.buffer, "sLightsBuffer");

  VkDescriptorBufferInfo matricesBufferInfo = {};
  matricesBufferInfo.buffer = sMatricesBuffer.buffer;
  matricesBufferInfo.offset = 0;
  matricesBufferInfo.range = VK_WHOLE_SIZE;

  VkDescriptorBufferInfo lightsBufferInfo = {};
  lightsBufferInfo.buffer = sLightsBuffer.buffer;
  lightsBufferInfo.offset = 0;
  lightsBufferInfo.range = VK_WHOLE_SIZE;

  absl::FixedArray<VkWriteDescriptorSet> writeDescriptorSets{
    {
      VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
      sGlobalDescriptorSet,              // dstSet
      0,                                 // dstBinding
      0,                                 // dstArrayElement
      1,                                 // descriptorCount
      VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, // descriptorType
      nullptr,                           // pImageInfo
      &matricesBufferInfo,               // pBufferInfo
      nullptr                            // pTexelBufferView
    },
    {
      VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
      sGlobalDescriptorSet,              // dstSet
      1,                                 // setBinding
      0,                                 // dstArrayElement
      1,                                 // descriptorCount
      VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, // descriptorType
      nullptr,                           // pImageInfo
      &lightsBufferInfo,                 // pBufferInfo
      nullptr                            // pTexelBufferView
    }};

  vkUpdateDescriptorSets(
    sDevice, gsl::narrow_cast<std::uint32_t>(writeDescriptorSets.size()),
    writeDescriptorSets.data(), 0, nullptr);

  /////
  //
  // Create the Image Blit Pipeline and shared state
  //
  /////

  VkSamplerCreateInfo imageBlitSamplerCI = {};
  imageBlitSamplerCI.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  imageBlitSamplerCI.magFilter = VK_FILTER_LINEAR;
  imageBlitSamplerCI.minFilter = VK_FILTER_LINEAR;
  imageBlitSamplerCI.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  imageBlitSamplerCI.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  imageBlitSamplerCI.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  imageBlitSamplerCI.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  imageBlitSamplerCI.mipLodBias = 0.f;
  imageBlitSamplerCI.anisotropyEnable = VK_FALSE;
  imageBlitSamplerCI.maxAnisotropy = 1;
  imageBlitSamplerCI.compareEnable = VK_FALSE;
  imageBlitSamplerCI.compareOp = VK_COMPARE_OP_ALWAYS;
  imageBlitSamplerCI.minLod = -1000.f;
  imageBlitSamplerCI.maxLod = 1000.f;
  imageBlitSamplerCI.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
  imageBlitSamplerCI.unnormalizedCoordinates = VK_FALSE;

  if (auto result = vkCreateSampler(sDevice, &imageBlitSamplerCI, nullptr,
                                    &sImageBlitSampler);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(make_error_code(result), "Cannot create sampler"));
  }

  NameObject(VK_OBJECT_TYPE_SAMPLER, sImageBlitSampler, "sImageBlitSampler");

  absl::FixedArray<VkDescriptorSetLayoutBinding>
    imageBlitDescriptorSetLayoutBindings{
      {
        0,                            // binding
        VK_DESCRIPTOR_TYPE_SAMPLER,   // descriptorType
        1,                            // descriptorCount
        VK_SHADER_STAGE_FRAGMENT_BIT, // stageFlags
        nullptr                       // pImmutableSamplers
      },
      {
        1,                                // binding
        VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, // descriptorType
        1,                                // descriptorCount
        VK_SHADER_STAGE_FRAGMENT_BIT,     // stageFlags
        nullptr                           // pImmutableSamplers
      }};

  VkDescriptorSetLayoutCreateInfo imageBlitDescriptorSetLayoutCI = {};
  imageBlitDescriptorSetLayoutCI.sType =
    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  imageBlitDescriptorSetLayoutCI.bindingCount = gsl::narrow_cast<std::uint32_t>(
    imageBlitDescriptorSetLayoutBindings.size());
  imageBlitDescriptorSetLayoutCI.pBindings =
    imageBlitDescriptorSetLayoutBindings.data();

  if (auto result =
        vkCreateDescriptorSetLayout(sDevice, &imageBlitDescriptorSetLayoutCI,
                                    nullptr, &sImageBlitDescriptorSetLayout);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(make_error_code(result),
                        "Cannot create Image Blit descriptor set layout"));
  }

  NameObject(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT,
             sImageBlitDescriptorSetLayout, "sImageBlitDescriptorSetLayout");

  VkDescriptorSetAllocateInfo imageBlitDescriptorSetAI = {};
  imageBlitDescriptorSetAI.sType =
    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  imageBlitDescriptorSetAI.descriptorPool = sDescriptorPool;
  imageBlitDescriptorSetAI.descriptorSetCount = 1;
  imageBlitDescriptorSetAI.pSetLayouts = &sImageBlitDescriptorSetLayout;

  if (auto result = vkAllocateDescriptorSets(sDevice, &imageBlitDescriptorSetAI,
                                             &sImageBlitDescriptorSet);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(
      make_error_code(result), "Cannot allocate Image Blit descriptor set"));
  }

  NameObject(VK_OBJECT_TYPE_DESCRIPTOR_SET, sImageBlitDescriptorSet,
             "sImageBlitDescriptorSet");

  absl::FixedArray<Shader> imageBlitShaders(2);

  if (auto vs = CompileShaderFromSource(sImageBlitVertexShaderSource,
                                        VK_SHADER_STAGE_VERTEX_BIT)) {
    imageBlitShaders[0] = std::move(*vs);
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(vs.error());
  }

  if (auto fs = CompileShaderFromSource(sImageBlitFragmentShaderSource,
                                        VK_SHADER_STAGE_FRAGMENT_BIT)) {
    imageBlitShaders[1] = std::move(*fs);
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(fs.error());
  }

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
    VK_FALSE,                            // blendEnable
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

  if (auto pipe = CreateRasterizationPipeline(
        imageBlitShaders, {}, {}, inputAssemblyStateCI, viewportStateCI,
        rasterizationStateCI, multisampleStateCI, depthStencilStateCI,
        colorBlendAttachmentStates, dynamicStates, 0,
        gsl::make_span(&sImageBlitDescriptorSetLayout, 1))) {
    sImageBlitPipeline = std::move(*pipe);
  } else {
    using namespace std::string_literals;
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(
      pipe.error().code(),
      "Cannot create Image Blit pipeline: "s + pipe.error().what()));
  }

  NameObject(VK_OBJECT_TYPE_PIPELINE_LAYOUT, sImageBlitPipeline.layout,
             "sImageBlitPipeline.layout");
  NameObject(VK_OBJECT_TYPE_PIPELINE, sImageBlitPipeline.pipeline,
             "sImageBlitPipeline.pipeline");

  /////
  //
  // Create UI Pipeline and other shared state
  //
  /////

  absl::FixedArray<VkDescriptorSetLayoutBinding> uiDescriptorSetLayoutBindings{
    {
      0,                            // binding
      VK_DESCRIPTOR_TYPE_SAMPLER,   // descriptorType
      1,                            // descriptorCount
      VK_SHADER_STAGE_FRAGMENT_BIT, // stageFlags
      nullptr                       // pImmutableSamplers
    },
    {
      1,                                // binding
      VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, // descriptorType
      1,                                // descriptorCount
      VK_SHADER_STAGE_FRAGMENT_BIT,     // stageFlags
      nullptr                           // pImmutableSamplers
    }};

  VkDescriptorSetLayoutCreateInfo uiDescriptorSetLayoutCI = {};
  uiDescriptorSetLayoutCI.sType =
    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  uiDescriptorSetLayoutCI.bindingCount =
    gsl::narrow_cast<std::uint32_t>(uiDescriptorSetLayoutBindings.size());
  uiDescriptorSetLayoutCI.pBindings = uiDescriptorSetLayoutBindings.data();

  if (auto result = vkCreateDescriptorSetLayout(
        sDevice, &uiDescriptorSetLayoutCI, nullptr, &sUIDescriptorSetLayout);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(
      make_error_code(result), "Cannot create UI descriptor set layout"));
  }

  NameObject(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, sUIDescriptorSetLayout,
             "sUIDescriptorSetLayout");

  VkDescriptorSetAllocateInfo uiDescriptorSetAI = {};
  uiDescriptorSetAI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  uiDescriptorSetAI.descriptorPool = sDescriptorPool;
  uiDescriptorSetAI.descriptorSetCount = 1;
  uiDescriptorSetAI.pSetLayouts = &sUIDescriptorSetLayout;

  if (auto result = vkAllocateDescriptorSets(sDevice, &uiDescriptorSetAI,
                                             &sUIDescriptorSet);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(
      make_error_code(result), "Cannot allocate UI descriptor set"));
  }

  NameObject(VK_OBJECT_TYPE_DESCRIPTOR_SET, sUIDescriptorSet,
             "sUIDescriptorSet");

  absl::FixedArray<Shader> uiShaders(2);

  if (auto vs = CompileShaderFromSource(sUIVertexShaderSource,
                                        VK_SHADER_STAGE_VERTEX_BIT)) {
    uiShaders[0] = std::move(*vs);
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(vs.error());
  }

  if (auto fs = CompileShaderFromSource(sUIFragmentShaderSource,
                                        VK_SHADER_STAGE_FRAGMENT_BIT)) {
    uiShaders[1] = std::move(*fs);
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(fs.error());
  }

  absl::FixedArray<VkPushConstantRange> pushConstantRanges{
    {VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::vec2) * 2}};

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

  colorBlendAttachmentStates[0].blendEnable = VK_TRUE;

  if (auto pipe = CreateRasterizationPipeline(
        uiShaders, vertexInputBindingDescriptions,
        vertexInputAttributeDescriptions, inputAssemblyStateCI, viewportStateCI,
        rasterizationStateCI, multisampleStateCI, depthStencilStateCI,
        colorBlendAttachmentStates, dynamicStates, 0,
        gsl::make_span(&sUIDescriptorSetLayout, 1))) {
    sUIPipeline = std::move(*pipe);
  } else {
    using namespace std::string_literals;
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(pipe.error().code(),
                        "Cannot create UI pipeline: "s + pipe.error().what()));
  }

  NameObject(VK_OBJECT_TYPE_PIPELINE_LAYOUT, sUIPipeline.layout,
             "sUIPipeline.layout");
  NameObject(VK_OBJECT_TYPE_PIPELINE, sUIPipeline.pipeline,
             "sUIPipeline.pipeline");

  sRunning = true;
  IRIS_LOG_LEAVE();
  return {};
} // iris::Renderer::Create

iris::Renderer::Features iris::Renderer::AvailableFeatures() noexcept {
  Expects(sInstance != VK_NULL_HANDLE);
  return sFeatures;
}

bool iris::Renderer::IsRunning() noexcept {
  return sRunning;
} // iris::Renderer::IsRunning

void iris::Renderer::Terminate() noexcept {
  IRIS_LOG_ENTER();
  sRunning = false;
  IRIS_LOG_LEAVE();
} // iris::Renderer::Terminate

VkRenderPass iris::Renderer::BeginFrame() noexcept {
  Expects(sRunning);
  Expects(!sInFrame);

  auto const currentTime = std::chrono::steady_clock::now();
  std::chrono::duration<float> const delta = currentTime - sPreviousFrameTime;
  sPreviousFrameTime = currentTime;

  decltype(sIOContinuations)::value_type ioContinuation;
  while (sIOContinuations.try_pop(ioContinuation)) {
    if (auto error = ioContinuation(); error.code()) {
      GetLogger()->error(error.what());
    }
  }

  auto&& windows = Windows();

  for (auto&& [title, window] : windows) {
    ImGui::SetCurrentContext(window.uiContext.get());

    window.platformWindow.PollEvents();
    if (ImGui::IsKeyReleased(wsi::Keys::kEscape)) Terminate();

    if (window.resized) {
      auto const newExtent = window.platformWindow.Extent();
      if (auto result =
            ResizeWindow(window, {newExtent.width, newExtent.height});
          !result) {
        GetLogger()->error("Error resizing window {}: {}", title,
                           result.error().what());
      } else {
        window.resized = false;
      }
    }

    ImGuiIO& io = ImGui::GetIO();

    io.DisplaySize = {static_cast<float>(window.extent.width),
                      static_cast<float>(window.extent.height)};
    io.DeltaTime = delta.count();

    io.KeyCtrl = ImGui::IsKeyDown(wsi::Keys::kLeftControl) |
                 ImGui::IsKeyDown(wsi::Keys::kRightControl);
    io.KeyShift = ImGui::IsKeyDown(wsi::Keys::kLeftShift) |
                  ImGui::IsKeyDown(wsi::Keys::kRightShift);
    io.KeyAlt = ImGui::IsKeyDown(wsi::Keys::kLeftAlt) |
                ImGui::IsKeyDown(wsi::Keys::kRightAlt);
    io.KeySuper = ImGui::IsKeyDown(wsi::Keys::kLeftSuper) |
                  ImGui::IsKeyDown(wsi::Keys::kRightSuper);

    io.MousePos = window.platformWindow.CursorPos();

    // TODO: update mouse cursor based on imgui request

    ImGui::NewFrame();
  }

  if (sFrameNum != 0) {
    VkFence frameFinishedFence =
      sFrameFinishedFences[(sFrameIndex - 1) % sNumFramesBuffered];

    if (auto result =
          vkWaitForFences(sDevice, 1, &frameFinishedFence, VK_TRUE, UINT64_MAX);
        result != VK_SUCCESS) {
      GetLogger()->error("Error waiting for frame finished fence: {}",
                         make_error_code(result).message());
    }

    if (auto result = vkResetFences(sDevice, 1, &frameFinishedFence);
        result != VK_SUCCESS) {
      GetLogger()->error("Error resetting frame finished fence: {}",
                         make_error_code(result).message());
    }
  }

  sInFrame = true;
  return sRenderPass;
} // iris::Renderer::BeginFrame()

void iris::Renderer::BindDescriptorSets(
  VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint,
  VkPipelineLayout layout, gsl::span<VkDescriptorSet> descriptorSets) noexcept {

  vkCmdBindDescriptorSets(commandBuffer,         // commandBuffer
                          pipelineBindPoint,     // pipelineBindPoint
                          layout,                // layout
                          0,                     // firstSet
                          1,                     // descriptorSetCount
                          &sGlobalDescriptorSet, // pDescriptorSets
                          0,                     // dynamicOffsetCount
                          nullptr                // pDynamicOffsets
  );

  vkCmdBindDescriptorSets(commandBuffer,     // commandBuffer
                          pipelineBindPoint, // pipelineBindPoint
                          layout,            // layout
                          1,                 // firstSet
                          gsl::narrow_cast<std::uint32_t>(
                            descriptorSets.size()), // descriptorSetCount
                          descriptorSets.data(),    // pDescriptorSets
                          0,                        // dynamicOffsetCount
                          nullptr                   // pDynamicOffsets
  );
} // iris::Renderer::BindDescriptorSets

void iris::Renderer::EndFrame(
  VkImageView view, gsl::span<const VkCommandBuffer> secondaryCBs) noexcept {
  Expects(sInFrame);

  auto previousFrameOldCBs = sOldCommandBuffers;
  sOldCommandBuffers.clear();

  auto&& windows = Windows();
  std::size_t const numWindows = windows.size();

  VkResult result;

  absl::FixedArray<VkSemaphore> waitSemaphores(numWindows);
  absl::FixedArray<VkSwapchainKHR> swapchains(numWindows);
  absl::FixedArray<std::uint32_t> imageIndices(numWindows);
  absl::FixedArray<VkCommandBuffer> commandBuffers(numWindows);

  absl::FixedArray<VkClearValue> clearValues(sNumRenderPassAttachments);
  clearValues[sDepthStencilTargetAttachmentIndex].depthStencil = {1.f, 0};

  VkCommandBufferBeginInfo commandBufferBI = {};
  commandBufferBI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  commandBufferBI.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

  VkRenderPassBeginInfo renderPassBI = {};
  renderPassBI.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  renderPassBI.renderPass = sRenderPass;
  renderPassBI.clearValueCount =
    gsl::narrow_cast<std::uint32_t>(clearValues.size());

  auto TextVector = [](char const* name, char const* componentFormat,
      int numComponents, float const* components) {
    ImGui::Text(name);
    ImGui::NextColumn();

    char label[32];
    for (int i = 0; i < numComponents; ++i) {
      std::snprintf(label, 32, componentFormat, components[i]);
      ImGui::Text(label);
      ImGui::NextColumn();
    }
  };

  auto TextMatrix = [](char const* name, char const* componentFormat,
      int numRows, int numCols, float const* components) {
    char label[32];
    for (int i = 0; i < numRows; ++i) {
      if (i == 0) {
        ImGui::Text(name);
        ImGui::NextColumn();
      } else {
        ImGui::Text("  ");
        ImGui::NextColumn();
      }

      for (int j = 0; j < numCols; ++j) {
        std::snprintf(label, 32, componentFormat, components[i * numCols + j]);
        ImGui::Text(label);
        ImGui::NextColumn();
      }
    }
  };

  for (auto&& [i, iter] : enumerate(windows)) {
    auto&& [title, window] = iter;
    ImGui::SetCurrentContext(window.uiContext.get());
    ImGuiIO& io = ImGui::GetIO();

    // TODO: move this ??
    sTrackball.Update(io);

    glm::vec3 const position = glm::vec3(0.f, 0.f, 0.f);
    glm::vec3 const center = glm::vec3(0.f, 1.f, 0.f);
    glm::vec3 const up = glm::vec3(0.f, 0.f, 1.f);
    sViewMatrix = glm::lookAt(position, center, up);

    // TODO: how to handle this at application scope?
    if (window.showUI && ImGui::Begin("Status")) {
      ImGui::Text("Last Frame %.3f ms", io.DeltaTime);
      ImGui::Text("Average %.3f ms/frame (%.1f FPS)", 1000.f / io.Framerate,
                  io.Framerate);

      ImGui::Separator();
      ImGui::BeginGroup();

      ImGui::Text("View");

      ImGui::Columns(5, NULL, false);
      TextVector("Position", "%+.3f", 3, glm::value_ptr(position));
      ImGui::Columns(1);

      ImGui::Columns(5, NULL, false);
      TextVector("Center", "%+.3f", 3, glm::value_ptr(center));
      ImGui::Columns(1);

      ImGui::Columns(5, NULL, false);
      TextVector("Up", "%+.3f", 3, glm::value_ptr(up));
      ImGui::Columns(1);

      ImGui::Columns(5, NULL, false);
      TextMatrix("Matrix", "%+.3f", 4, 4, glm::value_ptr(sViewMatrix));
      ImGui::Columns(1);

      ImGui::EndGroup();

      ImGui::Separator();
      ImGui::BeginGroup();

      ImGui::Text("Nav");
      ImGui::SameLine();
      if (ImGui::Button("Reset")) Nav::Reset();

      float const navResponse = Nav::Response();
      float const navScale = Nav::Scale();
      glm::vec3 const navPosition = Nav::Position();
      glm::quat const navOrientation = Nav::Orientation();
      glm::mat4 const navMatrix = Nav::Matrix();

      ImGui::Columns(5, NULL, false);
      TextVector("Response", "%+.3f", 1, &navResponse);
      ImGui::NextColumn();
      TextVector("Scale", "%+.3f", 1, &navScale);
      ImGui::Columns(1);

      ImGui::Columns(5, NULL, false);
      TextVector("Position", "%+.3f", 3, glm::value_ptr(navPosition));
      ImGui::Columns(1);

      ImGui::Columns(5, NULL, false);
      TextVector("Orientation", "%+.3f", 4, glm::value_ptr(navOrientation));
      ImGui::Columns(1);

      ImGui::Columns(5, NULL, false);
      TextMatrix("Matrix", "%+.3f", 4, 4, glm::value_ptr(navMatrix));
      ImGui::Columns(1);

      ImGui::EndGroup();

      ImGui::Separator();
      ImGui::BeginGroup();
      ImGui::Text("World");

      ImGui::Columns(5, NULL, false);
      TextVector("BSphere", "%+.3f", 4, glm::value_ptr(sWorldBoundingSphere));
      ImGui::Columns(1);

      ImGui::Columns(5, NULL, false);
      TextMatrix("Matrix", "%+.3f", 4, 4, glm::value_ptr(sWorldMatrix));
      ImGui::Columns(1);

      ImGui::EndGroup();
    }
    ImGui::End(); // Status

    ImGui::EndFrame();

    // currentFrame is still the previous frame, use that imageAvailable
    // semaphore. vkAcquireNextImageKHR will update frameIndex thereby updating
    // currentFrame (via frameIndex).
    window.imageAcquired = window.currentFrame().imageAvailable;

    if (result = vkAcquireNextImageKHR(sDevice, window.swapchain, UINT64_MAX,
                                       window.imageAcquired, VK_NULL_HANDLE,
                                       &window.frameIndex);
        result == VK_SUBOPTIMAL_KHR || result == VK_ERROR_OUT_OF_DATE_KHR) {
      GetLogger()->warn("Window {} swapchain out of date: resizing", title);
      auto const newExtent = window.platformWindow.Extent();

      if (auto r = ResizeWindow(window, {newExtent.width, newExtent.height});
          !r) {
        GetLogger()->error("Error resizing window {}: {}", title,
                           r.error().what());
      }

      result = vkAcquireNextImageKHR(sDevice, window.swapchain, UINT64_MAX,
                                     window.imageAcquired, VK_NULL_HANDLE,
                                     &window.frameIndex);
    }

    if (result != VK_SUCCESS) {
      GetLogger()->error("Error acquiring next image for window {}: {}", title,
                         make_error_code(result).message());
    }

    if (auto ptr = sMatricesBuffer.Map<MatricesBuffer*>()) {
      (*ptr)->ViewMatrix = sViewMatrix;
      (*ptr)->ViewMatrixInverse = glm::inverse(sViewMatrix);
      (*ptr)->ProjectionMatrix = window.projectionMatrix;
      (*ptr)->ProjectionMatrixInverse = window.projectionMatrixInverse;
      sMatricesBuffer.Unmap();
    } else {
      GetLogger()->error("Cannot update matrices buffer: {}",
                         ptr.error().what());
    }

    if (auto ptr = sLightsBuffer.Map<LightsBuffer*>()) {
      (*ptr)->Lights[0].direction = glm::vec4(0.f, -std::sqrt(2), std::sqrt(2), 0.f);
      (*ptr)->Lights[0].color = glm::vec4(.8f, .8f, .8f, 1.f);
      (*ptr)->NumLights = 1;
      sLightsBuffer.Unmap();
    } else {
      GetLogger()->error("Cannot update lights buffer: {}",
                         ptr.error().what());
    }

    Window::Frame& frame = window.currentFrame();

    if (result = vkResetCommandPool(sDevice, frame.commandPool, 0);
        result != VK_SUCCESS) {
      GetLogger()->error("Error resetting window {} frame {} command pool: {}",
                         title, window.frameIndex,
                         make_error_code(result).message());
    }

    if (result = vkBeginCommandBuffer(frame.commandBuffer, &commandBufferBI);
        result != VK_SUCCESS) {
      GetLogger()->error(
        "Error beginning window {} frame {} command buffer: {}", title,
        window.frameIndex, make_error_code(result).message());
    }

    vkCmdSetViewport(frame.commandBuffer, 0, 1, &window.viewport);
    vkCmdSetScissor(frame.commandBuffer, 0, 1, &window.scissor);

    PushConstants pushConstants;
    pushConstants.iMouse = {window.lastMousePos.x, window.lastMousePos.y, 0.f,
                            0.f};

    if (ImGui::IsMouseDown(iris::wsi::Buttons::kButtonLeft)) {
      window.lastMousePos.x = ImGui::GetIO().MousePos.x;
      window.lastMousePos.y = ImGui::GetIO().MousePos.y;
    }
    if (ImGui::IsMouseReleased(iris::wsi::Buttons::kButtonLeft)) {
      pushConstants.iMouse.z = ImGui::GetIO().MousePos.x;
      pushConstants.iMouse.w = ImGui::GetIO().MousePos.y;
    }

    pushConstants.iTimeDelta = ImGui::GetIO().DeltaTime;
    pushConstants.iTime = gsl::narrow_cast<float>(ImGui::GetTime());
    pushConstants.iFrame = gsl::narrow_cast<float>(ImGui::GetFrameCount());
    pushConstants.iFrameRate = pushConstants.iFrame / pushConstants.iTime;
    pushConstants.iResolution.x = ImGui::GetIO().DisplaySize.x;
    pushConstants.iResolution.y = ImGui::GetIO().DisplaySize.y;
    pushConstants.iResolution.z =
      pushConstants.iResolution.x / pushConstants.iResolution.y;

    clearValues[sColorTargetAttachmentIndex].color = window.clearColor;

    renderPassBI.framebuffer = frame.framebuffer;
    renderPassBI.renderArea.extent = window.extent;
    renderPassBI.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(frame.commandBuffer, &renderPassBI,
                         VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);

    if (view != VK_NULL_HANDLE) {
      VkCommandBuffer commandBuffer = BlitImage(
        view, &window.viewport, &window.scissor,
        gsl::make_span<std::byte>(reinterpret_cast<std::byte*>(&pushConstants),
                                  sizeof(PushConstants)));
      vkCmdExecuteCommands(frame.commandBuffer, 1, &commandBuffer);
      sOldCommandBuffers.push_back(commandBuffer);
    }

    { // this block locks sRenderables so that we can iterate over them safely
      std::lock_guard<decltype(sRenderables.mutex)> lck(sRenderables.mutex);
      for (auto&& [id, renderable] : sRenderables.renderables) {
        pushConstants.ModelMatrix =
          Nav::Matrix() * sWorldMatrix * renderable.modelMatrix;
        pushConstants.ModelViewMatrix = sViewMatrix * pushConstants.ModelMatrix;
        pushConstants.ModelViewMatrixInverse =
          glm::inverse(pushConstants.ModelViewMatrix);

        VkCommandBuffer commandBuffer =
          RenderRenderable(renderable, &window.viewport, &window.scissor,
                           gsl::make_span<std::byte>(
                             reinterpret_cast<std::byte*>(&pushConstants),
                             sizeof(PushConstants)));
        vkCmdExecuteCommands(frame.commandBuffer, 1, &commandBuffer);
        sOldCommandBuffers.push_back(commandBuffer);
      }
    }

    if (!secondaryCBs.empty()) {
      vkCmdExecuteCommands(frame.commandBuffer,
                           gsl::narrow_cast<std::uint32_t>(secondaryCBs.size()),
                           secondaryCBs.data());
    }

    if (window.showUI) {
      auto uiCBs = RenderUI(frame.commandBuffer, window);
      sOldCommandBuffers.insert(sOldCommandBuffers.end(), uiCBs.begin(),
                                uiCBs.end());
    }

    vkCmdEndRenderPass(frame.commandBuffer);
    if (result = vkEndCommandBuffer(frame.commandBuffer);
        result != VK_SUCCESS) {
      GetLogger()->error("Error ending window {} frame {} command buffer: {}",
                         title, window.frameIndex,
                         make_error_code(result).message());
    }

    waitSemaphores[i] = window.imageAcquired;
    swapchains[i] = window.swapchain;
    imageIndices[i] = window.frameIndex;
    commandBuffers[i] = frame.commandBuffer;
  }

  absl::FixedArray<VkPipelineStageFlags> waitDstStages(
    numWindows, VK_PIPELINE_STAGE_TRANSFER_BIT);

  VkSubmitInfo submitI = {};
  submitI.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitI.waitSemaphoreCount = gsl::narrow_cast<std::uint32_t>(numWindows);
  submitI.pWaitSemaphores = waitSemaphores.data();
  submitI.pWaitDstStageMask = waitDstStages.data();
  submitI.commandBufferCount =
    gsl::narrow_cast<std::uint32_t>(commandBuffers.size());
  submitI.pCommandBuffers = commandBuffers.data();

  if (!swapchains.empty()) {
    submitI.signalSemaphoreCount = 1;
    submitI.pSignalSemaphores = &sImagesReadyForPresent;
  }

  VkFence frameFinishedFence = sFrameFinishedFences[sFrameIndex];

  if (result =
        vkQueueSubmit(sCommandQueues[0], 1, &submitI, frameFinishedFence);
      result != VK_SUCCESS) {
    GetLogger()->error("Error submitting command buffer: {}",
                       iris::to_string(result));
  }

  if (!swapchains.empty()) {
    absl::FixedArray<VkResult> presentResults(numWindows);

    VkPresentInfoKHR presentI = {};
    presentI.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentI.waitSemaphoreCount = 1;
    presentI.pWaitSemaphores = &sImagesReadyForPresent;
    presentI.swapchainCount = gsl::narrow_cast<std::uint32_t>(numWindows);
    presentI.pSwapchains = swapchains.data();
    presentI.pImageIndices = imageIndices.data();
    presentI.pResults = presentResults.data();

    if (result = vkQueuePresentKHR(sCommandQueues[0], &presentI);
        result != VK_SUCCESS) {
      GetLogger()->error("Error presenting swapchains: {}",
                         iris::to_string(result));
    }
  }

  vkFreeCommandBuffers(sDevice, sCommandPools[0],
      gsl::narrow_cast<std::uint32_t>(previousFrameOldCBs.size()),
      previousFrameOldCBs.data());

  sFrameNum += 1;
  sFrameIndex = sFrameNum % sNumFramesBuffered;
  sInFrame = false;
} // iris::Renderer::EndFrame

iris::Renderer::RenderableID
iris::Renderer::AddRenderable(Component::Renderable renderable) noexcept {
  IRIS_LOG_ENTER();

  GetLogger()->debug("renderable bsphere: {:+.3f} {:+.3f} {:+.3f} {:+.3f}",
      renderable.boundingSphere.x, renderable.boundingSphere.y,
      renderable.boundingSphere.z, renderable.boundingSphere.w);
  glm::vec3 const renderableBSCenter(renderable.boundingSphere.x,
                                     renderable.boundingSphere.y,
                                     renderable.boundingSphere.z);
  float const renderableBSRadius = renderable.boundingSphere.w;

  glm::vec3 const worldBSCenter(sWorldBoundingSphere.x, sWorldBoundingSphere.y,
                                sWorldBoundingSphere.z);
  float const worldBSRadius = sWorldBoundingSphere.w;

  float const rwBSLength = glm::length(renderableBSCenter - worldBSCenter);
  float const wrBSLength = glm::length(worldBSCenter - renderableBSCenter);

  if (rwBSLength + renderableBSRadius < worldBSRadius) {
    // do nothing, world encompasses renderable
  } else if (wrBSLength + worldBSRadius < renderableBSRadius) {
    sWorldBoundingSphere = renderable.boundingSphere;
  } else {
    float const r = (renderableBSRadius + worldBSRadius + rwBSLength) / 2.f;
    //glm::vec3 const c =
      //glm::mix(renderableBSCenter, worldBSCenter - renderableBSCenter,
               //r - renderableBSRadius);
    glm::vec3 const c =
      renderableBSCenter + (worldBSCenter - renderableBSCenter) *
                             (r - renderableBSRadius) / wrBSLength;
    sWorldBoundingSphere = glm::vec4(c, r);
  }

  auto&& id = sRenderables.Insert(std::move(renderable));

  IRIS_LOG_LEAVE();
  return id;
} // iris::Renderer::AddRenderable

tl::expected<void, std::system_error>
iris::Renderer::RemoveRenderable(RenderableID const& id) noexcept {
  IRIS_LOG_ENTER();

  class ReleaseTask : public tbb::task {
  public:
    ReleaseTask(Component::Renderable r) noexcept
      : renderable_(std::move(r)) {}

    tbb::task* execute() override {
      IRIS_LOG_ENTER();

      if (renderable_.indexBuffer) DestroyBuffer(renderable_.indexBuffer);
      if (renderable_.vertexBuffer) DestroyBuffer(renderable_.vertexBuffer);

      for (auto&& buffer : renderable_.buffers) {
        if (buffer) DestroyBuffer(buffer);
      }

      for (auto&& sampler : renderable_.textureSamplers) {
        if (sampler != VK_NULL_HANDLE) {
          vkDestroySampler(sDevice, sampler, nullptr);
        }
      }

      for (auto&& view : renderable_.textureViews) {
        if (view != VK_NULL_HANDLE) {
          vkDestroyImageView(sDevice, view, nullptr);
        }
      }

      for (auto&& image : renderable_.textures) {
        if (image) DestroyImage(image);
      }

      if (renderable_.descriptorSet != VK_NULL_HANDLE) {
        vkFreeDescriptorSets(sDevice, sDescriptorPool, 1,
                             &renderable_.descriptorSet);
      }

      if (renderable_.descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(sDevice, renderable_.descriptorSetLayout,
                                     nullptr);
      }

      if (renderable_.pipeline) DestroyPipeline(renderable_.pipeline);

      IRIS_LOG_LEAVE();
      return nullptr;
    }

  private:
    Component::Renderable renderable_;
  }; // struct IOTask

  if (auto old = sRenderables.Remove(id)) {
    try {
      ReleaseTask* task = new (tbb::task::allocate_root()) ReleaseTask(*old);
      tbb::task::enqueue(*task);
    } catch (std::exception const& e) {
      IRIS_LOG_LEAVE();
      return tl::unexpected(
        std::system_error(make_error_code(Error::kEnqueueError),
                          fmt::format("Enqueing release task: {}", e.what())));
    }
  }

  IRIS_LOG_LEAVE();
  return {};
} // iris::Renderer::RemoveRenderable

tl::expected<iris::Renderer::CommandQueue, std::system_error>
iris::Renderer::AcquireCommandQueue(
  std::chrono::milliseconds timeout) noexcept {
  IRIS_LOG_ENTER();
  Expects(sDevice != VK_NULL_HANDLE);
  std::unique_lock<std::timed_mutex> lock(sCommandQueueMutex, timeout);
  if (!lock) return tl::unexpected(std::system_error(Error::kTimeout));

  if (sCommandQueueHead == sCommandQueues.size() &&
      sCommandQueueFree > sCommandQueues.size()) {
    return tl::unexpected(std::system_error(Error::kNoCommandQueuesFree,
                                            "all command queues are in use"));
  }

  CommandQueue commandQueue;

  if (sCommandQueueHead < sCommandQueues.size()) {
    commandQueue.id = sCommandQueueHead++;
  } else {
    return tl::unexpected(std::system_error(Error::kNotImplemented));
  }

  commandQueue.queueFamilyIndex = sQueueFamilyIndex;
  commandQueue.queue = sCommandQueues[commandQueue.id];
  commandQueue.commandPool = sCommandPools[commandQueue.id];
  commandQueue.submitFence = sCommandFences[commandQueue.id];

  IRIS_LOG_LEAVE();
  return commandQueue;
} // iris::Renderer::AcquireCommandQueue

tl::expected<void, std::system_error> iris::Renderer::ReleaseCommandQueue(
  CommandQueue& queue, std::chrono::milliseconds timeout) noexcept {
  IRIS_LOG_ENTER();
  std::unique_lock<std::timed_mutex> lock(sCommandQueueMutex, timeout);
  if (!lock) return tl::unexpected(std::system_error(Error::kTimeout));

  if (queue.id == sCommandQueues.size() - 1) {
    queue.id = UINT32_MAX;
    queue.queueFamilyIndex = UINT32_MAX;
    queue.queue = VK_NULL_HANDLE;
    queue.commandPool = VK_NULL_HANDLE;
    queue.submitFence = VK_NULL_HANDLE;
    sCommandQueueHead--;
  } else {
    GetLogger()->critical("not implemented");
    std::abort();
  }

  IRIS_LOG_LEAVE();
  return {};
} // iris::Renderer::ReleaseCommandQueue

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

// TODO: Would it be better to return a future instead of void?
tl::expected<void, std::system_error>
iris::Renderer::LoadFile(std::filesystem::path const& path) noexcept {
  IRIS_LOG_ENTER();

  class IOTask : public tbb::task {
  public:
    IOTask(std::filesystem::path p) noexcept
      : path_(std::move(p)) {}

    tbb::task* execute() override {
      IRIS_LOG_ENTER();

      GetLogger()->debug("Loading {}", path_.string());
      auto const& ext = path_.extension();

      if (ext.compare(".json") == 0) {
        sIOContinuations.push(io::LoadJSON(path_));
      } else if (ext.compare(".gltf") == 0) {
        sIOContinuations.push(io::LoadGLTF(path_));
      } else {
        GetLogger()->error("Unhandled file extension '{}' for {}", ext.string(),
                           path_.string());
      }

      IRIS_LOG_LEAVE();
      return nullptr;
    }

  private:
    std::filesystem::path path_;
  }; // struct IOTask

  try {
    IOTask* task = new (tbb::task::allocate_root()) IOTask(path);
    tbb::task::enqueue(*task);
  } catch (std::exception const& e) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(
      make_error_code(Error::kEnqueueError),
      fmt::format("Enqueing IO task for {}: {}", path.string(), e.what())));
  }

  IRIS_LOG_LEAVE();
  return {};
} // iris::Renderer::LoadFile

tl::expected<void, std::system_error> iris::Renderer::ProcessControlMessage(
  iris::Control::Control const& controlMessage) noexcept {
  IRIS_LOG_ENTER();

  switch (controlMessage.type_case()) {
  case iris::Control::Control::TypeCase::kDisplays:
    for (int i = 0; i < controlMessage.displays().windows_size(); ++i) {
      CreateEmplaceWindow(controlMessage.displays().windows(i));
    }
    break;
  case iris::Control::Control::TypeCase::kWindow:
    CreateEmplaceWindow(controlMessage.window());
    break;
  case iris::Control::Control::TypeCase::kShaderToy:
    sIOContinuations.push(io::LoadShaderToy(controlMessage.shadertoy()));
    break;
  default:
    GetLogger()->error("Unsupported controlMessage message type {}",
                       controlMessage.type_case());
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(Error::kControlMessageInvalid,
                        fmt::format("Unsupported controlMessage type {}",
                                    controlMessage.type_case())));
    break;
  }

  IRIS_LOG_LEAVE();
  return {};
} // iris::Renderer::ProcessControlMessage

