#include "renderer/command_buffers.h"
#include "logging.h"

tl::expected<iris::Renderer::CommandBuffers, std::system_error>
iris::Renderer::CommandBuffers::Allocate(VkCommandPool pool,
                                         std::uint32_t count,
                                         VkCommandBufferLevel level) noexcept {
  IRIS_LOG_ENTER();
  Expects(sDevice != VK_NULL_HANDLE);
  Expects(pool != VK_NULL_HANDLE);

  CommandBuffers buffers;
  buffers.pool = pool;
  buffers.buffers.resize(count);

  VkCommandBufferAllocateInfo commandBufferAI = {};
  commandBufferAI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  commandBufferAI.commandPool = pool;
  commandBufferAI.level = level;
  commandBufferAI.commandBufferCount = count;

  if (auto result = vkAllocateCommandBuffers(sDevice, &commandBufferAI,
                                             buffers.buffers.data());
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(make_error_code(result),
                                            "Cannot allocate command buffers"));
  }

  Ensures(buffers.buffers.size() > 0);
  IRIS_LOG_LEAVE();
  return std::move(buffers);
} // iris::Renderer::CommandBuffers::Allocate

iris::Renderer::CommandBuffers::CommandBuffers(CommandBuffers&& other) noexcept
  : pool(other.pool)
  , buffers(std::move(other.buffers)) {
  other.buffers.clear();
} // iris::Renderer::CommandBuffers::CommandBuffers

iris::Renderer::CommandBuffers& iris::Renderer::CommandBuffers::
operator=(CommandBuffers&& rhs) noexcept {
  if (this == &rhs) return *this;

  pool = rhs.pool;
  buffers = std::move(rhs.buffers);

  rhs.buffers.clear();

  return *this;
} // iris::Renderer::CommandBuffers::operator=

iris::Renderer::CommandBuffers::~CommandBuffers() noexcept {
  if (buffers.empty()) return;
  IRIS_LOG_ENTER();
  Expects(sDevice != VK_NULL_HANDLE);

  vkFreeCommandBuffers(sDevice, pool,
                       gsl::narrow_cast<std::uint32_t>(buffers.size()),
                       buffers.data());
  buffers.clear();

  IRIS_LOG_LEAVE();
} // iris::Renderer::CommandBuffers::~CommandBuffers
