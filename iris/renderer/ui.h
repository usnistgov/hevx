#ifndef HEV_IRIS_RENDERER_UI_H_
#define HEV_IRIS_RENDERER_UI_H_

#include "renderer/vulkan.h"
#include "glm/glm.hpp"
#include "tl/expected.hpp"
#include <system_error>

namespace iris::Renderer::ui {

std::error_code Initialize() noexcept;

std::error_code BeginFrame(glm::vec2 const& size) noexcept;

tl::expected<VkCommandBuffer, std::error_code>
EndFrame(VkFramebuffer framebuffer) noexcept;

void Shutdown() noexcept;

} // namespace iris::Renderer::ui

#endif // HEV_IRIS_RENDERER_UI_H_
