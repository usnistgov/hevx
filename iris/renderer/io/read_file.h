#ifndef HEV_IRIS_RENDERER_IO_H_
#define HEV_IRIS_RENDERER_IO_H_
/*! \file
 * \brief \ref iris::Renderer::io declaration.
 */

#include "expected.hpp"
#include <cstddef>
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

/*! \brief Blocking function to directly read a file.
 */
tl::expected<std::vector<std::byte>, std::system_error>
ReadFile(filesystem::path const& path) noexcept;

} // namespace iris::Renderer::io

#endif // HEV_IRIS_RENDERER_IO_H_
