#ifndef HEV_IRIS_RENDERER_RENDERER_H_
#define HEV_IRIS_RENDERER_RENDERER_H_
/*! \file
 * \brief \ref iris::Renderer declaration.
 */

#include "expected.hpp"
#if STD_FS_IS_EXPERIMENTAL
#include <experimental/filesystem>
namespace filesystem = std::experimental::filesystem;
#else
#include <filesystem>
namespace filesystem = std::filesystem;
#endif
#include "gsl/gsl"
#include "spdlog/sinks/sink.h"
#include <cstdint>
#include <system_error>
#include <type_traits>

// Forward declare the iris::Control::Control class
namespace iris::Control {
class Control;
} // namespace iris::Control

/*! \brief IRIS renderer
 *
 * There is a single renderer per application-instance. The expected application
 * flow is shown in the below diagram. \ref iris-viewer.cc is the main rendering
 * application.
 * \dotfile appflow.gv
 */
namespace iris::Renderer {

/*!
 * Rendering initialization options.
 */
enum class Options {
  kNone = (0),                     //<! No special behavior.
  kReportDebugMessages = (1 << 0), //<! Report API debug messages.
  kUseValidationLayers = (1 << 1), //<! Use API validation layers.
};

/*! \brief Initialize the rendering system.
 *
 * There is only a single renderer per application instance.
 * \param[in] appName the name of the application.
 * \param[in] options the \ref Options to initialize the renderer with.
 * \param[in] appVersion the version of the application.
 * \param[in] logSinks a list of sinks to log to.
 * \return \ref std::system_error
 */
[[nodiscard]] std::system_error
Initialize(gsl::czstring<> appName, Options const& options,
           std::uint32_t appVersion = 0,
           spdlog::sinks_init_list logSinks = {}) noexcept;

/*! \brief Request the rendering system to shutdown.
 */
void Terminate() noexcept;

/*! \brief Indicates if the rendering system is running.
 *
 * The rendering system is considered running until \ref Terminate is called
 * or any window is closed.
 * \return true if the renderer is running, false if not.
 */
bool IsRunning() noexcept;

/*! \brief Begin the next rendering frame.
 *
 * This must be called each time through the rendering loop after calling \ref
 * EndFrame.
 */
void BeginFrame() noexcept;

/*! \brief End the next rendering frame.
 *
 * This must be called each time through the rendering loop after calling \ref
 * BeginFrame.
 */
void EndFrame() noexcept;

/*! \brief Load a file into the rendering system.
 *
 * This is an async load operation, so the only errors returned are if the
 * operation failed to be enqueued.
 * \param[in] path The path to load.
 * \return std::error_code indicating if the async operation failed to be
 * enqueued.
 */
[[nodiscard]] std::error_code LoadFile(filesystem::path const& path) noexcept;

/*! \brief Execute a control message.
 *
 * This is a synchronous execution step.
 * \param[in] control the \ref iris::Control::Control message.
 * \return std::error_code indicating if the message failed.
 */
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
