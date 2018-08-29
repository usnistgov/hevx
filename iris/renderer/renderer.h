#ifndef HEV_IRIS_RENDERER_H_
#define HEV_IRIS_RENDERER_H_
/*! \file
 * \brief \ref iris::Renderer declaration.
 */

#include "gsl/gsl"
#include <cstdint>
#include <system_error>

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
std::error_code Initialize(gsl::not_null<gsl::czstring<>> appName,
                           std::uint32_t appVersion = 0) noexcept;

} // namespace iris::Renderer

#endif // HEV_IRIS_RENDERER_H_

