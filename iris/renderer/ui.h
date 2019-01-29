#ifndef HEV_IRIS_RENDERER_UI_H_
#define HEV_IRIS_RENDERER_UI_H_

#include "glm/glm.hpp"
#include "imgui.h"
#include "iris/renderer/buffer.h"
#include "iris/renderer/command_buffers.h"
#include "iris/renderer/descriptor_sets.h"
#include "iris/renderer/image.h"
#include "iris/renderer/impl.h"
#include "iris/renderer/pipeline.h"
#include <memory>
#include <system_error>

namespace iris::Renderer {

struct UI {
  tl::expected<std::unique_ptr<iris::Renderer::UI>, std::system_error>
  static Create() noexcept;

  static constexpr std::size_t const kNumCommandBuffers = 2;
  static constexpr std::size_t const kNumDescriptorSets = 1;

  CommandBuffers commandBuffers;
  std::uint32_t commandBufferIndex{0};
  Image fontImage{};
  ImageView fontImageView{};
  Sampler fontImageSampler{};
  Buffer vertexBuffer{};
  Buffer indexBuffer{};
  DescriptorSets descriptorSets;
  Pipeline pipeline{};
  std::unique_ptr<ImGuiContext, decltype(&ImGui::DestroyContext)> context;

  [[nodiscard]] std::system_error BeginFrame(float frameDelta) noexcept;

  tl::expected<VkCommandBuffer, std::system_error>
  EndFrame(VkFramebuffer framebuffer) noexcept;

  UI()
  noexcept
    : commandBuffers(kNumCommandBuffers)
    , descriptorSets(kNumDescriptorSets)
    , context(nullptr, &ImGui::DestroyContext) {}
}; // struct UI

} // namespace iris::Renderer

#endif // HEV_IRIS_RENDERER_UI_H_
