#ifndef HEV_IRIS_RENDERER_H_
#define HEV_IRIS_RENDERER_H_

#include "expected.hpp"
#if STD_FS_IS_EXPERIMENTAL
#include <experimental/filesystem>
namespace filesystem = std::experimental::filesystem;
#else
#include <filesystem>
namespace filesystem = std::filesystem;
#endif
#include "gsl/gsl"
#include "iris/config.h"
#include "iris/vulkan.h"
#include "iris/window.h"
#include "spdlog/sinks/sink.h"
#include <cstddef>
#include <cstdint>
#include <exception>
#include <string>
#include <system_error>
#include <type_traits>

namespace iris {

namespace Control {
class Control;
} // namespace Control

namespace Renderer {

/*!
 * Rendering initialization options.
 */
enum class Options {
  kReportDebugMessages = (1 << 0), //<! Report API debug messages.
  kUseValidationLayers = (1 << 1), //<! Use API validation layers.
};

[[nodiscard]] tl::expected<void, std::system_error>
Initialize(gsl::czstring<> appName, Options const& options,
           std::uint32_t appVersion, spdlog::sinks_init_list logSinks) noexcept;

/*! \brief Indicates if the rendering system is running.
 *
 * The rendering system is considered running until \ref Terminate is called
 * or any window is closed.
 * \return true if the renderer is running, false if not.
 */
[[nodiscard]] bool IsRunning() noexcept;

/*! \brief Request the rendering system to shutdown.
 */
void Terminate() noexcept;

[[nodiscard]] tl::expected<Window, std::exception>
CreateWindow(gsl::czstring<> title, wsi::Offset2D offset, wsi::Extent2D extent,
             glm::vec4 const& clearColor, Window::Options const& options,
             int display, std::uint32_t numFrames) noexcept;

tl::expected<void, std::system_error>
ResizeWindow(Window& window, VkExtent2D newExtent) noexcept;

[[nodiscard]] tl::expected<VkCommandBuffer, std::system_error>
BeginOneTimeSubmit() noexcept;

tl::expected<void, std::system_error>
EndOneTimeSubmit(VkCommandBuffer commandBuffer) noexcept;

tl::expected<void, std::system_error>
TransitionImage(VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout,
                std::uint32_t mipLevels, std::uint32_t arrayLayers) noexcept;

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
 * \return std::system_error indicating if the async operation failed to be
 * enqueued.
 */
[[nodiscard]] tl::expected<void, std::system_error>
LoadFile(filesystem::path const& path) noexcept;

/*! \brief Execute a control message.
 *
 * This is a synchronous execution step.
 * \param[in] control the \ref iris::Control::Control message.
 * \return std::system_error> indicating if the message failed.
 */
tl::expected<void, std::system_error>
Control(iris::Control::Control const& control) noexcept;

//! \brief bit-wise or of \ref Renderer::Options.
inline Options operator|(Options const& lhs, Options const& rhs) noexcept {
  using U = std::underlying_type_t<Options>;
  return static_cast<Options>(static_cast<U>(lhs) | static_cast<U>(rhs));
}

//! \brief bit-wise or of \ref Options.
inline Options operator|=(Options& lhs, Options const& rhs) noexcept {
  lhs = lhs | rhs;
  return lhs;
}

//! \brief bit-wise and of \ref Options.
inline Options operator&(Options const& lhs, Options const& rhs) noexcept {
  using U = std::underlying_type_t<Options>;
  return static_cast<Options>(static_cast<U>(lhs) & static_cast<U>(rhs));
}

//! \brief bit-wise and of \ref Options.
inline Options operator&=(Options& lhs, Options const& rhs) noexcept {
  lhs = lhs & rhs;
  return lhs;
}

} // namespace Renderer

} // namespace iris

#endif // HEV_IRIS_RENDERER_H_
