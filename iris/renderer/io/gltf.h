#ifndef HEV_IRIS_RENDERER_IO_GLTF_H_
#define HEV_IRIS_RENDERER_IO_GLTF_H_

#include "iris/renderer/io.h"

namespace iris::Renderer::io {

tl::expected<std::function<void(void)>, std::system_error>
LoadGLTF(filesystem::path const& path) noexcept;

} // namespace iris::Renderer::io

#endif // HEV_IRIS_RENDERER_IO_GLTF_H_
