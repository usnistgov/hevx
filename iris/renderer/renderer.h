#ifndef HEV_IRIS_RENDERER_RENDERER_H_
#define HEV_IRIS_RENDERER_RENDERER_H_
/*! \file
 * \brief \ref iris::Renderer declaration.
 */

#include "gsl/gsl"
#include "spdlog/sinks/sink.h"
#include "tl/expected.hpp"
#include <cstdint>
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

enum class Options {
  kNone = (0),
  kReportDebugMessages = (1 << 0),
  kUseValidationLayers = (1 << 1),
};

/*! \brief Initialize the rendering system.
 *
 * There is only a single renderer per application instance.
 * \param[in] appName the name of the application.
 * \param[in] appVersion the version of the application.
 * \return \ref Error
 */
std::error_code Initialize(gsl::czstring<> appName, Options const& options,
                           std::uint32_t appVersion = 0,
                           spdlog::sinks_init_list logSinks = {}) noexcept;
void Shutdown() noexcept;

void Terminate() noexcept;
bool IsRunning() noexcept;

void Frame() noexcept;

std::error_code Control(iris::Control::Control const& control) noexcept;

//! \brief bit-wise or of \ref Options.
inline Options operator|(Options const& lhs, Options const& rhs) noexcept {
  using U = std::underlying_type_t<Options>;
  return static_cast<Options>(static_cast<U>(lhs) | static_cast<U>(rhs));
}

//! \brief bit-wise and of \ref Options.
inline Options operator&(Options const& lhs, Options const& rhs) noexcept {
  using U = std::underlying_type_t<Options>;
  return static_cast<Options>(static_cast<U>(lhs) & static_cast<U>(rhs));
}

} // namespace iris::Renderer

#endif // HEV_IRIS_RENDERER_RENDERER_H_

