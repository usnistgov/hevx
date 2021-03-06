#ifndef HEV_IRIS_IO_JSON_H_
#define HEV_IRIS_IO_JSON_H_

#include <filesystem>
#include <functional>
#include <system_error>

namespace iris::io {

std::function<std::system_error(void)>
LoadJSON(std::filesystem::path const& path) noexcept;

} // namespace iris::io

#endif // HEV_IRIS_IO_JSON_H_
