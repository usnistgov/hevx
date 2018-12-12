#ifndef HEV_IRIS_RENDERER_IO_GLTF_H_
#define HEV_IRIS_RENDERER_IO_GLTF_H_

#if STD_FS_IS_EXPERIMENTAL
#include <experimental/filesystem>
namespace filesystem = std::experimental::filesystem;
#else
#include <filesystem>
namespace filesystem = std::filesystem;
#endif
#include <functional>
#include <system_error>

namespace iris::Renderer::io {

std::function<std::system_error(void)>
LoadGLTF(filesystem::path const& path) noexcept;

} // namespace iris::Renderer::io

#endif // HEV_IRIS_RENDERER_IO_GLTF_H_
