#ifndef HEV_IRIS_RENDERER_RENDERER_H_
#define HEV_IRIS_RENDERER_RENDERER_H_
/*! \file
 * \brief \ref iris::Renderer declaration.
 */

#include "gsl/gsl"
#include "spdlog/sinks/sink.h"
#include "tl/expected.hpp"
#include <cstdint>
#include <string_view>
#include <system_error>

namespace iris::Control {
class Control;
} // namespace iris::Control

/*! \brief IRIS renderer
 *
 * There is a single renderer per application-instance. That renderer must be
 * initialized with a call to Renderer::Initialize.
 */
namespace iris::Renderer {

/*! \brief Initialize the rendering system.
 *
 * There is only a single renderer per application instance.
 * \param[in] appName the name of the application.
 * \param[in] appVersion the version of the application.
 * \return \ref Error
 */
std::error_code Initialize(gsl::czstring<> appName,
                           std::uint32_t appVersion = 0,
                           spdlog::sinks_init_list logSinks = {}) noexcept;
void Shutdown() noexcept;

void Terminate() noexcept;
bool IsRunning() noexcept;

void Frame() noexcept;

std::error_code Control(iris::Control::Control const& control) noexcept;

} // namespace iris::Renderer

#endif // HEV_IRIS_RENDERER_RENDERER_H_

