#ifndef HEV_IRIS_RENDERER_IO_H_
#define HEV_IRIS_RENDERER_IO_H_
/*! \file
 * \brief \ref iris::Renderer declaration.
 */

#include <cstdint>
#include <string_view>
#include <system_error>

namespace iris::Renderer::io {

std::error_code LoadFile(std::string_view fileName) noexcept;

} // namespace iris::Renderer::io

#endif // HEV_IRIS_RENDERER_IO_H_

