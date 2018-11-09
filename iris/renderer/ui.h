#ifndef HEV_IRIS_RENDERER_UI_H_
#define HEV_IRIS_RENDERER_UI_H_

#include "renderer/vulkan.h"
#include "glm/glm.hpp"
#include "tl/expected.hpp"
#include <system_error>

namespace iris::Renderer::ui {

tl::expected<void, std::system_error> Initialize() noexcept;

tl::expected<void, std::system_error>
BeginFrame(glm::vec2 const& displaySize,
           glm::vec2 const& mousePos = {-FLT_MAX, -FLT_MAX}) noexcept;

tl::expected<VkCommandBuffer, std::system_error>
EndFrame(VkFramebuffer framebuffer) noexcept;

void Shutdown() noexcept;

} // namespace iris::Renderer::ui

#endif // HEV_IRIS_RENDERER_UI_H_
