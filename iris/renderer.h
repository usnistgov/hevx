/*!
\file
\brief public Renderer API
*/
#ifndef HEV_IRIS_RENDERER_H_
#define HEV_IRIS_RENDERER_H_

#include "iris/config.h"

#include "iris/buffer.h"
#include "iris/components/material.h"
#include "iris/types.h"
#include "iris/vulkan.h"

#if PLATFORM_COMPILER_MSVC
#include <codeanalysis/warnings.h>
#pragma warning(push)
#pragma warning(disable : ALL_CODE_ANALYSIS_WARNINGS)
#pragma warning(disable : ALL_CPPCORECHECK_WARNINGS)
#endif

#include "glm/gtc/quaternion.hpp"
#include "glm/vec4.hpp"
#include "gsl/gsl"
#include "io/gltf.h"
#include "spdlog/sinks/sink.h"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <string>
#include <system_error>
#include <type_traits>

#if PLATFORM_COMPILER_MSVC
#pragma warning(pop)
#endif

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

/*!
\namespace Renderer::Component
\brief the \ref Renderer components.

\see \ref ECS for a brief overview of the Entity Component System (ECS).
*/
namespace Renderer::Component {
struct Material;
struct Renderable;
struct Traceable;
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
*/
namespace Renderer {

/*!
Rendering initialization options.
*/
enum class Options {
  kReportDebugMessages = (1 << 0), //<! Report API debug messages.
  kEnableValidation = (1 << 1),    //<! Enable API validation.
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
[[nodiscard]] expected<void, std::system_error>
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

std::uint64_t CurrentFrameNum() noexcept;

float LastFrameDelta() noexcept;
float TotalTime() noexcept;

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

\param[in] secondaryCBs a list of secondary command buffers that will be
executed into the primary command buffer for each \ref Window.
*/
void EndFrame(gsl::span<const VkCommandBuffer> secondaryCBs = {}) noexcept;

using MaterialID = ComponentID<struct MaterialIDTab>;

MaterialID AddMaterial(Component::Material material) noexcept;

expected<void, std::system_error> RemoveMaterial(MaterialID const& id) noexcept;

using RenderableID = ComponentID<struct RenderableIDTag>;

/*!
\brief Add a \ref Component::Renderable for rendering each frame.

\param[in] renderable the Component::Renderable to add.
\return the \ref RenderableID of the added renderable.
*/
RenderableID AddRenderable(Component::Renderable renderable) noexcept;

/*!
\brief Remove a \ref Component::Renderable by its \ref RenderableID.

\param[in] id the RenderableID to remove.
*/
expected<void, std::system_error>
RemoveRenderable(RenderableID const& id) noexcept;

void SetTraceable(Component::Traceable traceable) noexcept;

struct CommandQueue {
  std::uint32_t id{UINT32_MAX};
  std::uint32_t queueFamilyIndex{UINT32_MAX};
  VkQueue queue{VK_NULL_HANDLE};
  VkCommandPool commandPool{VK_NULL_HANDLE};
  VkFence submitFence{VK_NULL_HANDLE};
}; // struct CommandQueue

expected<CommandQueue, std::system_error> AcquireCommandQueue(
  std::chrono::milliseconds timeout = std::chrono::milliseconds{
    INT64_MAX}) noexcept;

expected<void, std::system_error> ReleaseCommandQueue(
  CommandQueue& queue,
  std::chrono::milliseconds timeout = std::chrono::milliseconds{
    INT64_MAX}) noexcept;

[[nodiscard]] expected<VkCommandBuffer, std::system_error>
BeginOneTimeSubmit(VkCommandPool commandPool) noexcept;

expected<void, std::system_error>
EndOneTimeSubmit(VkCommandBuffer commandBuffer, VkCommandPool commandPool,
                 VkQueue queue, VkFence fence) noexcept;

/*!
\brief Load a file into the rendering system.

This is an async load operation, so the only errors returned are if the
operation failed to be enqueued.

\param[in] path The path to load.

\return std::system_error indicating if the async operation failed to be
enqueued.
*/
[[nodiscard]] expected<void, std::system_error>
LoadFile(std::filesystem::path const& path) noexcept;

/*!
\brief Execute a control message.

This is a synchronous execution step and not thread safe.
\param[in] control the \ref iris::Control::Control message.
\return std::system_error> indicating if the message failed.
*/
expected<void, std::system_error>
ProcessControlMessage(iris::Control::Control const& control) noexcept;

namespace Nav {

// all of these are currently not thread safe

float Response() noexcept;
void SetResponse(float response) noexcept;

float Scale() noexcept;
void Rescale(float scale) noexcept;

glm::vec3 Position() noexcept;
void Move(glm::vec3 const& delta) noexcept;
void Reposition(glm::vec3 position) noexcept;

glm::quat Orientation() noexcept;
void Rotate(glm::quat const& delta) noexcept;
void Reorient(glm::quat orientation) noexcept;

glm::mat4 Matrix() noexcept;

void Reset() noexcept;

} // namespace Nav

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

namespace std {

template <>
struct hash<iris::Renderer::MaterialID> {
  std::size_t operator()(iris::Renderer::MaterialID const& v) const noexcept {
    return std::hash<iris::Renderer::MaterialID::id_type>{}(v());
  }
};

template <>
struct hash<iris::Renderer::RenderableID> {
  std::size_t operator()(iris::Renderer::RenderableID const& v) const noexcept {
    return std::hash<iris::Renderer::RenderableID::id_type>{}(v());
  }
};

} // namespace std

#endif // HEV_IRIS_RENDERER_H_
