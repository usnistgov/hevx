#ifndef HEV_IRIS_RENDERER_UI_H_
#define HEV_IRIS_RENDERER_UI_H_

#include "glm/glm.hpp"
#include "imgui.h"
#include "iris/renderer/buffer.h"
#include "iris/renderer/descriptor_set.h"
#include "iris/renderer/image.h"
#include "iris/renderer/impl.h"
#include "iris/renderer/pipeline.h"
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
  Image fontImage{};
  ImageView fontImageView{};
  Sampler fontImageSampler{};
  Buffer vertexBuffer{};
  Buffer indexBuffer{};
  DescriptorSet descriptorSet{};
  Pipeline pipeline{};
  std::unique_ptr<ImGuiContext, decltype(&ImGui::DestroyContext)> context;
  TimePoint previousTime{};

  UI() noexcept : context(nullptr, &ImGui::DestroyContext) {}
  UI(UI const&) = delete;
  UI(UI&& other) noexcept = default;
  UI& operator=(UI const&) = delete;
  UI& operator=(UI&& other) noexcept = default;
  ~UI() noexcept;
}; // struct UI

} // namespace iris::Renderer

#endif // HEV_IRIS_RENDERER_UI_H_
