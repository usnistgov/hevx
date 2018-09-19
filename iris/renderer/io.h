#ifndef HEV_IRIS_RENDERER_IO_H_
#define HEV_IRIS_RENDERER_IO_H_
/*! \file
 * \brief \ref iris::Renderer declaration.
 */

#include "tl/expected.hpp"
#include <experimental/filesystem>
#include <system_error>
#include <vector>

namespace iris::Renderer::io {

tl::expected<std::vector<char>, std::error_code>
ReadFile(std::experimental::filesystem::path path) noexcept;

void LoadFile(std::experimental::filesystem::path path) noexcept;

} // namespace iris::Renderer::io

#endif // HEV_IRIS_RENDERER_IO_H_

