#ifndef HEV_IRIS_RENDERER_SURFACE_H_
#define HEV_IRIS_RENDERER_SURFACE_H_

#include "iris/renderer/vulkan.h"
#include "iris/wsi/window.h"
#include "tl/expected.hpp"
#include <system_error>

namespace iris::Renderer {

struct Surface {
  static tl::expected<Surface, std::error_code>
  Create(wsi::Window& window) noexcept;

  VkSurfaceKHR handle;
}; // struct Surface

} // namespace iris::Renderer

#endif // HEV_IRIS_RENDERER_SURFACE_H_
