#ifndef HEV_IRIS_RENDERER_IO_H_
#define HEV_IRIS_RENDERER_IO_H_
/*! \file
 * \brief \ref iris::Renderer declaration.
 */

#include "tl/expected.hpp"
#if STD_FS_IS_EXPERIMENTAL
#include <experimental/filesystem>
namespace filesystem = std::experimental::filesystem;
#else
#include <filesystem>
namespace filesystem = std::filesystem;
#endif
#include <system_error>
#include <vector>

namespace iris::Renderer::io {

tl::expected<std::vector<char>, std::error_code> ReadFile(
    filesystem::path path) noexcept;

  void LoadFile(filesystem::path path) noexcept;

} // namespace iris::Renderer::io

#endif // HEV_IRIS_RENDERER_IO_H_

