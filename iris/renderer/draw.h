#ifndef HEV_IRIS_RENDERER_DRAW_H_
#define HEV_IRIS_RENDERER_DRAW_H_

#include "iris/renderer/buffer.h"
#include "iris/renderer/pipeline.h"

namespace iris::Renderer {

struct DrawData {
  Pipeline pipeline{};

  VkIndexType indexType{VK_INDEX_TYPE_UINT16};
  std::uint32_t indexCount{0};
  std::uint32_t vertexCount{0};

  Buffer indexBuffer{};
  Buffer vertexBuffer{};

  DrawData() noexcept = default;
  DrawData(DrawData const&) = delete;
  DrawData(DrawData&&) noexcept = default;
  DrawData& operator=(DrawData const&) = delete;
  DrawData& operator=(DrawData&&) noexcept = default;
  ~DrawData() noexcept = default;
}; // struct DrawData

extern std::vector<iris::Renderer::DrawData>& DrawCommands();

} // namespace iris::Renderer

#endif // HEV_IRIS_RENDERER_DRAW_H_
