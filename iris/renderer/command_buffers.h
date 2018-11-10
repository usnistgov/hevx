#ifndef HEV_IRIS_RENDERER_COMMAND_BUFFERS_H_
#define HEV_IRIS_RENDERER_COMMAND_BUFFERS_H_

#include "absl/container/inlined_vector.h"
#include "iris/renderer/impl.h"
#include <system_error>

namespace iris::Renderer {

struct CommandBuffers {
  static tl::expected<CommandBuffers, std::system_error> Allocate(
    VkCommandPool pool, std::uint32_t count,
    VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY) noexcept;

  VkCommandPool pool;
  absl::InlinedVector<VkCommandBuffer, 32> buffers;

  VkCommandBuffer operator[](std::size_t index) const noexcept {
    return buffers[index];
  }

  std::size_t size() const noexcept { return buffers.size(); }

  CommandBuffers() = default;
  CommandBuffers(CommandBuffers const&) = delete;
  CommandBuffers(CommandBuffers&& other) noexcept;
  CommandBuffers& operator=(CommandBuffers const&) = delete;
  CommandBuffers& operator=(CommandBuffers&& rhs) noexcept;
  ~CommandBuffers() noexcept;

private:
  std::string name;
}; // struct CommandBuffers

} // namespace iris::Renderer

#endif // HEV_IRIS_RENDERER_COMMAND_BUFFERS_H_
