#ifndef HEV_IRIS_RENDERER_IO_H_
#define HEV_IRIS_RENDERER_IO_H_
/*! \file
 * \brief \ref iris::Renderer::io declaration.
 */

#include "tl/expected.hpp"
#include <cstddef>
#if STD_FS_IS_EXPERIMENTAL
#include <experimental/filesystem>
namespace filesystem = std::experimental::filesystem;
#else
#include <filesystem>
namespace filesystem = std::filesystem;
#endif
#include <functional>
#include <system_error>
#include <vector>

namespace iris::Renderer::io {

/*! \brief Initialize the IO system.
 */
std::error_code Initialize() noexcept;

/*! \brief Shutdownt the IO system.
 */
std::error_code Shutdown() noexcept;

std::vector<std::function<void(void)>> GetResults() noexcept;

/*! \brief Non-blocking function to asynchronously load a file.
 */
void LoadFile(filesystem::path path) noexcept;

/*! \brief Blocking function to directly read a file.
 */
tl::expected<std::vector<std::byte>, std::system_error>
ReadFile(filesystem::path path) noexcept;

} // namespace iris::Renderer::io

#endif // HEV_IRIS_RENDERER_IO_H_
