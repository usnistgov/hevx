#ifndef HEV_IRIS_IO_SAVG_H_
#define HEV_IRIS_IO_SAVG_H_

#include <filesystem>
#include <functional>
#include <system_error>

namespace iris::io {

std::function<std::system_error(void)>
LoadSAVG(std::filesystem::path const& path) noexcept;

} // namespace iris::io

#endif // HEV_IRIS_IO_SAVG_H_
