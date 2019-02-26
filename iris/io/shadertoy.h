#ifndef HEV_IRIS_IO_SHADERTOY_H_
#define HEV_IRIS_IO_SHADERTOY_H_

#include "iris/protos.h"
#include <functional>
#include <system_error>

namespace iris::io {

std::function<std::system_error(void)>
LoadShaderToy(Control::ShaderToy const& message) noexcept;

} // namespace iris::io

#endif // HEV_IRIS_IO_SHADERTOY_H_

