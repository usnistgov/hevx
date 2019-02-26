#ifndef HEV_IRIS_RENDERER_UTIL_H_
#define HEV_IRIS_RENDERER_UTIL_H_

#include "absl/container/flat_hash_map.h"
#if PLATFORM_COMPILER_GCC
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#include "absl/container/inlined_vector.h"
#if PLATFORM_COMPILER_GCC
#pragma GCC diagnostic pop
#endif
#include "glm/vec3.hpp"
#include "iris/renderer.h"
#include <mutex>
#include <vector>

namespace iris::Renderer {

extern VkInstance sInstance;
extern VkDebugUtilsMessengerEXT sDebugUtilsMessenger;
extern VkPhysicalDevice sPhysicalDevice;
extern VkDevice sDevice;
extern VmaAllocator sAllocator;

extern std::uint32_t sGraphicsQueueFamilyIndex;
extern absl::InlinedVector<VkQueue, 16> sGraphicsCommandQueues;
extern absl::InlinedVector<VkCommandPool, 16> sGraphicsCommandPools;
extern absl::InlinedVector<VkFence, 16> sGraphicsCommandFences;

extern VkRenderPass sRenderPass;

extern std::uint32_t const sNumRenderPassAttachments;
extern std::uint32_t const sColorTargetAttachmentIndex;
extern std::uint32_t const sColorResolveAttachmentIndex;
extern std::uint32_t const sDepthStencilTargetAttachmentIndex;
extern std::uint32_t const sDepthStencilResolveAttachmentIndex;

extern VkSurfaceFormatKHR const sSurfaceColorFormat;
extern VkFormat const sSurfaceDepthStencilFormat;
extern VkSampleCountFlagBits const sSurfaceSampleCount;
extern VkPresentModeKHR const sSurfacePresentMode;

absl::flat_hash_map<std::string, iris::Window>& Windows();

class Renderables {
public:
  std::vector<Component::Renderable> operator()() {
    std::lock_guard<std::mutex> lock{mutex_};
    return renderables_;
  }

  void push_back(Component::Renderable renderable) {
    std::lock_guard<std::mutex> lock{mutex_};
    renderables_.clear();
    renderables_.push_back(std::move(renderable));
  }

private:
  std::mutex mutex_{};
  std::vector<Component::Renderable> renderables_{};
}; // class Renderables

extern Renderables sRenderables;

struct ShaderToyPushConstants {
    glm::vec4 iMouse;
    float iTime;
    float iTimeDelta;
    float iFrameRate;
    float iFrame;
    glm::vec3 iResolution;
    float padding0;
}; // struct ShaderToyPushConstants

/////
//
// FIXME: MUST re-work this once I've got it working
//
/////

tl::expected<absl::FixedArray<VkCommandBuffer>, std::system_error>
AllocateCommandBuffers(VkCommandBufferLevel level, std::uint32_t count) noexcept;

struct Shader {
  VkShaderModule handle;
  VkShaderStageFlagBits stage;
}; // struct Shader

tl::expected<VkShaderModule, std::system_error>
CompileShaderFromSource(std::string_view source, VkShaderStageFlagBits stage,
                        std::string name = {}) noexcept;

tl::expected<std::pair<VkPipelineLayout, VkPipeline>, std::system_error>
CreateGraphicsPipeline(
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
  std::uint32_t renderPassSubpass, std::string name = {}) noexcept;

void AddRenderable(Component::Renderable renderable) noexcept;

VkCommandBuffer
RenderRenderable(iris::Renderer::Component::Renderable const& renderable,
                 VkViewport* pViewport, VkRect2D* pScissor) noexcept;

} // namespace iris::Renderer

#endif // HEV_IRIS_RENDERER_UTIL_H_
