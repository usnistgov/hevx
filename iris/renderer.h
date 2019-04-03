/*!
\file
\brief public Renderer API
*/
#ifndef HEV_IRIS_RENDERER_H_
#define HEV_IRIS_RENDERER_H_

#include "iris/config.h"

#include "expected.hpp"
#if STD_FS_IS_EXPERIMENTAL
#include <experimental/filesystem>
namespace filesystem = std::experimental::filesystem;
#else
#include <filesystem>
namespace filesystem = std::filesystem;
#endif
#include "glm/vec4.hpp"
#include "gsl/gsl"
#include "iris/buffer.h"
#include "iris/vulkan.h"
#include "spdlog/sinks/sink.h"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <string>
#include <system_error>
#include <type_traits>

/*!
\namespace iris
\brief The Immersive Runtime Interactive System is the low-level renderer of
HEV.

The main public entry points to IRIS are in the Renderer namespace.
*/
namespace iris {

namespace Control {
class Control;
} // namespace Control

namespace Renderer::Component {
struct Renderable;
} // namespace Renderer::Component

/*!
\namespace iris::Renderer
\brief The public API for rendering.

The expected application flow is:
- Renderer::Initialize
- Renderer::LoadFile (repeatedly to load all files on command line)
- while Renderer::IsRunning:
  - Renderer::BeginFrame
  - Renderer::EndFrame

\dotfile appflow.gv "Expected application flow"
*/
namespace Renderer {

/*!
Rendering initialization options.
*/
enum class Options {
  kReportDebugMessages = (1 << 0), //<! Report API debug messages.
  kUseValidationLayers = (1 << 1), //<! Use API validation layers.
};

/*!
Rendering features available
*/
enum class Features {
  kNone = (1 << 0),       //! No features
  kRayTracing = (1 << 1), //! Renderer has ray tracing support.
};

/*!
\brief Initialize the Rendering system

\param[in] appName the application name
\param[in] options the Options for configuring the Renderer
\param[in] logSinks a list of spdlog::sink sinks to use for logging.
\param[in] appVersion the application version (default: iris::Renderer version)

\return void or a std::system_error indicating the reason for failure.
*/
[[nodiscard]] tl::expected<void, std::system_error>
Initialize(gsl::czstring<> appName, Options const& options,
           spdlog::sinks_init_list logSinks,
           std::uint32_t appVersion = 0) noexcept;

/*!
\brief Get the available Features for an initialized Renderer.
\return the available Features for an initialized Renderer.
*/
Features AvailableFeatures() noexcept;

/*!
\brief Indicates if the rendering system is running.

The rendering system is considered running until \ref Terminate is called or
any window is closed.

\return true if the renderer is running, false if not.
*/
[[nodiscard]] bool IsRunning() noexcept;

/*!
\brief Request the rendering system to shutdown.
*/
void Terminate() noexcept;

/*!
\brief Begin the next rendering frame.

This must be called each time through the rendering loop after calling \ref
EndFrame.

\return the VkRenderPass to use for any secondary command buffers submitted to
\ref EndFrame
*/
VkRenderPass BeginFrame() noexcept;

/*!
\brief End the next rendering frame.

This must be called each time through the rendering loop after calling \ref
BeginFrame.

\param[in] image a VkImage that will be copied into the current framebuffer of
each \ref Window.
\param[in] secondaryCBs a list of secondary command buffers that will be
executed into the primary command buffer for each \ref Window.
*/
void EndFrame(VkImage image = VK_NULL_HANDLE,
              gsl::span<const VkCommandBuffer> secondaryCBs = {}) noexcept;

/*!
\brief Add a \ref Component::Renderable for rendering each frame.

\param[in] renderable the Component::Renderable to add
*/
void AddRenderable(Component::Renderable renderable) noexcept;

struct CommandQueue {
  std::uint32_t id{UINT32_MAX};
  std::uint32_t queueFamilyIndex{UINT32_MAX};
  VkQueue queue{VK_NULL_HANDLE};
  VkCommandPool commandPool{VK_NULL_HANDLE};
  VkFence submitFence{VK_NULL_HANDLE};
}; // struct CommandQueue

tl::expected<CommandQueue, std::system_error> AcquireCommandQueue(
  std::chrono::milliseconds timeout = std::chrono::milliseconds{
    INT64_MAX}) noexcept;

tl::expected<void, std::system_error> ReleaseCommandQueue(
  CommandQueue& queue,
  std::chrono::milliseconds timeout = std::chrono::milliseconds{
    INT64_MAX}) noexcept;

/*!
\brief Load a file into the rendering system.

This is an async load operation, so the only errors returned are if the
operation failed to be enqueued.

\param[in] path The path to load.

\return std::system_error indicating if the async operation failed to be
enqueued.
*/
[[nodiscard]] tl::expected<void, std::system_error>
LoadFile(filesystem::path const& path) noexcept;

/*!
\brief Execute a control message.

This is a synchronous execution step.
\param[in] control the \ref iris::Control::Control message.
\return std::system_error> indicating if the message failed.
*/
tl::expected<void, std::system_error>
Control(iris::Control::Control const& control) noexcept;

/*!
\brief bit-wise or of \ref Renderer::Options.
*/
inline Options operator|(Options const& lhs, Options const& rhs) noexcept {
  using U = std::underlying_type_t<Options>;
  return static_cast<Options>(static_cast<U>(lhs) | static_cast<U>(rhs));
}

/*!
\brief bit-wise or of \ref Options.
*/
inline Options operator|=(Options& lhs, Options const& rhs) noexcept {
  lhs = lhs | rhs;
  return lhs;
}

/*!
\brief bit-wise and of \ref Options.
*/
inline Options operator&(Options const& lhs, Options const& rhs) noexcept {
  using U = std::underlying_type_t<Options>;
  return static_cast<Options>(static_cast<U>(lhs) & static_cast<U>(rhs));
}

/*!
\brief bit-wise and of \ref Options.
*/
inline Options operator&=(Options& lhs, Options const& rhs) noexcept {
  lhs = lhs & rhs;
  return lhs;
}

/*!
\brief bit-wise or of \ref Renderer::Features.
*/
inline Features operator|(Features const& lhs, Features const& rhs) noexcept {
  using U = std::underlying_type_t<Features>;
  return static_cast<Features>(static_cast<U>(lhs) | static_cast<U>(rhs));
}

/*!
\brief bit-wise or of \ref Features.
*/
inline Features operator|=(Features& lhs, Features const& rhs) noexcept {
  lhs = lhs | rhs;
  return lhs;
}

/*!
\brief bit-wise and of \ref Features.
*/
inline Features operator&(Features const& lhs, Features const& rhs) noexcept {
  using U = std::underlying_type_t<Features>;
  return static_cast<Features>(static_cast<U>(lhs) & static_cast<U>(rhs));
}

/*!
\brief bit-wise and of \ref Features.
*/
inline Features operator&=(Features& lhs, Features const& rhs) noexcept {
  lhs = lhs & rhs;
  return lhs;
}

} // namespace Renderer

} // namespace iris

#endif // HEV_IRIS_RENDERER_H_
