#ifndef HEV_IRIS_IO_GLTF_H_
#define HEV_IRIS_IO_GLTF_H_

#include "nlohmann/json.hpp"
#include <filesystem>
#include <functional>
#include <system_error>

namespace iris::io {

using json = nlohmann::json;

std::function<std::system_error(void)>
LoadGLTF(std::filesystem::path const& path) noexcept;

std::function<std::system_error(void)>
LoadGLTF(json const& gltf) noexcept;

} // namespace iris::io

#endif // HEV_IRIS_IO_GLTF_H_
