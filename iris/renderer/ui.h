#ifndef HEV_IRIS_RENDERER_UI_H_
#define HEV_IRIS_RENDERER_UI_H_

#include "absl/container/inlined_vector.h"
#include "glm/glm.hpp"
#include "imgui.h"
#include "iris/renderer/buffer.h"
#include "iris/renderer/command_buffers.h"
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

  static constexpr std::size_t const kNumCommandBuffers = 2;
  static constexpr std::size_t const kNumDescriptorSets = 1;

  CommandBuffers commandBuffers;
  std::uint32_t commandBufferIndex{0};
  Image fontImage{};
  ImageView fontImageView{};
  Sampler fontImageSampler{};
  Buffer vertexBuffer{};
  Buffer indexBuffer{};
  DescriptorSet descriptorSet;
  Pipeline pipeline{};
  std::unique_ptr<ImGuiContext, decltype(&ImGui::DestroyContext)> context;
  TimePoint previousTime{};

  UI()
  noexcept
    : commandBuffers(kNumCommandBuffers)
    , descriptorSet(kNumDescriptorSets)
    , context(nullptr, &ImGui::DestroyContext) {}
}; // struct UI

} // namespace iris::Renderer

#endif // HEV_IRIS_RENDERER_UI_H_
