#ifndef HEV_IRIS_RENDERER_IO_JSON_H_
#define HEV_IRIS_RENDERER_IO_JSON_H_

#include "iris/renderer/io/io.h"

namespace iris::Renderer::io {

tl::expected<std::function<void(void)>, std::system_error>
LoadJSON(filesystem::path const& path) noexcept;

} // namespace iris::Renderer::io

#endif // HEV_IRIS_RENDERER_IO_JSON_H_
