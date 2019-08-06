#ifndef HEV_IRIS_IO_READ_FILE_H_
#define HEV_IRIS_IO_READ_FILE_H_

#include "iris/types.h"
#include <cstddef>
#include <filesystem>
#include <system_error>
#include <vector>

namespace iris::io {

/*! \brief Blocking function to directly read a file.
 */
expected<std::vector<std::byte>, std::system_error>
ReadFile(std::filesystem::path const& path) noexcept;

} // namespace iris::io

#endif // HEV_IRIS_IO_READ_FILE_H_
