#ifndef HEV_IRIS_IO_GLTF_H_
#define HEV_IRIS_IO_GLTF_H_

#include "nlohmann/json.hpp"
#if STD_FS_IS_EXPERIMENTAL
#include <experimental/filesystem>
namespace filesystem = std::experimental::filesystem;
#else
#include <filesystem>
namespace filesystem = std::filesystem;
#endif
#include <functional>
#include <system_error>

namespace iris::io {

using json = nlohmann::json;

std::function<std::system_error(void)>
LoadGLTF(filesystem::path const& path) noexcept;

std::function<std::system_error(void)>
LoadGLTF(json const& gltf) noexcept;

} // namespace iris::io

#endif // HEV_IRIS_IO_GLTF_H_
