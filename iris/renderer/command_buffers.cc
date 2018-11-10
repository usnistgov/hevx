#include "renderer/command_buffers.h"
#include "logging.h"

tl::expected<iris::Renderer::CommandBuffers, std::system_error>
iris::Renderer::CommandBuffers::Allocate(VkCommandPool pool,
                                         std::uint32_t count,
                                         VkCommandBufferLevel level) noexcept {
  IRIS_LOG_ENTER();
  Expects(sDevice != VK_NULL_HANDLE);
  Expects(pool != VK_NULL_HANDLE);

  CommandBuffers buffers(count);
  buffers.pool = pool;

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
  , buffers(other.buffers.size())
  , name(std::move(other.name)) {
  for (std::size_t i = 0; i < buffers.size(); ++i) {
    buffers[i] = other.buffers[i];
  }

  other.pool = VK_NULL_HANDLE;
} // iris::Renderer::CommandBuffers::CommandBuffers

iris::Renderer::CommandBuffers& iris::Renderer::CommandBuffers::
operator=(CommandBuffers&& rhs) noexcept {
  if (this == &rhs) return *this;
  Expects(buffers.size() == rhs.buffers.size());

  pool = rhs.pool;
  for (std::size_t i = 0; i < buffers.size(); ++i) buffers[i] = rhs.buffers[i];
  name = std::move(rhs.name);

  rhs.pool = VK_NULL_HANDLE;

  return *this;
} // iris::Renderer::CommandBuffers::operator=

iris::Renderer::CommandBuffers::~CommandBuffers() noexcept {
  if (pool == VK_NULL_HANDLE) return;
  IRIS_LOG_ENTER();
  Expects(sDevice != VK_NULL_HANDLE);

  vkFreeCommandBuffers(sDevice, pool,
                       gsl::narrow_cast<std::uint32_t>(buffers.size()),
                       buffers.data());
  IRIS_LOG_LEAVE();
} // iris::Renderer::CommandBuffers::~CommandBuffers
