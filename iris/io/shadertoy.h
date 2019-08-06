#ifndef HEV_IRIS_IO_SHADERTOY_H_
#define HEV_IRIS_IO_SHADERTOY_H_

#include "iris/components/renderable.h"
#include "iris/error.h"
#include "iris/types.h"
#include <functional>
#include <system_error>

namespace iris::io {

expected<Renderer::Component::Renderable, std::system_error>
LoadShaderToy(std::string const& url);

} // namespace iris::io

#endif // HEV_IRIS_IO_SHADERTOY_H_

