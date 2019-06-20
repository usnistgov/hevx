#ifndef HEV_IRIS_IO_SHADERTOY_H_
#define HEV_IRIS_IO_SHADERTOY_H_

#include "expected.hpp"
#include "iris/components/renderable.h"
#include "iris/error.h"
#include <functional>
#include <system_error>

namespace iris::io {

tl::expected<Renderer::Component::Renderable, std::system_error>
LoadShaderToy(std::string const& url);

} // namespace iris::io

#endif // HEV_IRIS_IO_SHADERTOY_H_

