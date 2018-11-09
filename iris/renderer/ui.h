#ifndef HEV_IRIS_RENDERER_UI_H_
#define HEV_IRIS_RENDERER_UI_H_

#include "glm/glm.hpp"
#include "renderer/buffer.h"
#include "renderer/impl.h"
#include "imgui.h"
#include "tl/expected.hpp"
#include <chrono>
#include <memory>
#include <system_error>

namespace iris::Renderer {

struct UI {
  using Duration = std::chrono::duration<float, std::ratio<1, 1>>;
  using TimePoint =
    std::chrono::time_point<std::chrono::steady_clock, Duration>;

  tl::expected<iris::Renderer::UI, std::system_error>
  static Create() noexcept;

  std::vector<VkCommandBuffer> commandBuffers{};
  std::uint32_t commandBufferIndex{0};
  VkImage fontImage{VK_NULL_HANDLE};
  VmaAllocation fontImageAllocation{VK_NULL_HANDLE};
  VkImageView fontImageView{VK_NULL_HANDLE};
  VkSampler fontImageSampler{VK_NULL_HANDLE};
  Buffer vertexBuffer{};
  Buffer indexBuffer{};
  VkDescriptorSetLayout descriptorSetLayout{VK_NULL_HANDLE};
  std::vector<VkDescriptorSet> descriptorSets;
  VkPipelineLayout pipelineLayout{VK_NULL_HANDLE};
  VkPipeline pipeline{VK_NULL_HANDLE};
  std::unique_ptr<ImGuiContext, decltype(&ImGui::DestroyContext)> context;
  TimePoint previousTime{};

  UI() noexcept : context(nullptr, &ImGui::DestroyContext) {}
  UI(UI const&) = delete;
  UI(UI&& other) noexcept;
  UI& operator=(UI const&) = delete;
  UI& operator=(UI&& other) noexcept;
  ~UI() noexcept;
}; // struct UI

} // namespace iris::Renderer

#endif // HEV_IRIS_RENDERER_UI_H_
