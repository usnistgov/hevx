/*! \file
 * \brief \ref iris::Renderer definition.
 */
#include "config.h"
#include "renderer/renderer.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/inlined_vector.h"
#include "error.h"
#if PLATFORM_COMPILER_GCC
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#endif
#include "glslang/Public/ShaderLang.h"
#if PLATFORM_COMPILER_GCC
#pragma GCC diagnostic pop
#endif
#include "protos.h"
#include "renderer/io/json.h"
#include "renderer/vulkan_support.h"
#if PLATFORM_COMPILER_MSVC
#pragma warning(push)
#pragma warning(disable : 4127)
#endif
#include "spdlog/spdlog.h"
#if PLATFORM_COMPILER_MSVC
#pragma warning(pop)
#endif
#include "tbb/concurrent_queue.h"
#include "tbb/task.h"
#include "tbb/task_scheduler_init.h"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <utility>

/////
//
// The logging must be directly defined here instead of including "logging.h".
// This is because we need to define the static logger instance in this file.
//
/////
namespace iris {

static spdlog::logger*
GetLogger(spdlog::sinks_init_list logSinks = {}) noexcept {
  static std::shared_ptr<spdlog::logger> sLogger;
  if (!sLogger) {
    sLogger = std::make_shared<spdlog::logger>("iris", logSinks);
    sLogger->set_level(spdlog::level::trace);
    spdlog::register_logger(sLogger);
    spdlog::set_pattern("[%Y-%m-%d %T.%e] [%t] [%n] %^[%l] %v%$");
  }

  return sLogger.get();
}

} // namespace iris

#ifndef NDEBUG

//! \brief Logs entry into a function.
#define IRIS_LOG_ENTER()                                                       \
  do {                                                                         \
    ::iris::GetLogger()->trace("ENTER: {} ({}:{})", __func__, __FILE__,        \
                               __LINE__);                                      \
    ::iris::GetLogger()->flush();                                              \
  } while (false)

//! \brief Logs leave from a function.
#define IRIS_LOG_LEAVE()                                                       \
  do {                                                                         \
    ::iris::GetLogger()->trace("LEAVE: {} ({}:{})", __func__, __FILE__,        \
                               __LINE__);                                      \
    ::iris::GetLogger()->flush();                                              \
  } while (false)

#else

#define IRIS_LOG_ENTER()
#define IRIS_LOG_LEAVE()

#endif

namespace iris::Renderer {

static VkInstance sInstance{VK_NULL_HANDLE};
static VkDebugUtilsMessengerEXT sDebugUtilsMessenger{VK_NULL_HANDLE};
static VkPhysicalDevice sPhysicalDevice{VK_NULL_HANDLE};
static VkDevice sDevice{VK_NULL_HANDLE};
static std::uint32_t sGraphicsQueueFamilyIndex{UINT32_MAX};
static VkQueue sGraphicsCommandQueue{VK_NULL_HANDLE};
static VmaAllocator sAllocator{VK_NULL_HANDLE};

static bool sInitialized{false};
static std::atomic_bool sRunning{false};

static tbb::task_scheduler_init sTaskSchedulerInit{
  tbb::task_scheduler_init::deferred};
static tbb::concurrent_queue<std::function<std::system_error(void)>>
  sIOContinuations;

static std::chrono::steady_clock::time_point sPreviousFrameTime;
static std::uint64_t sFrameNum = 0;

} // namespace iris::Renderer

std::system_error
iris::Renderer::Initialize(gsl::czstring<> appName, Options const& options,
                           std::uint32_t appVersion,
                           spdlog::sinks_init_list logSinks) noexcept {
  GetLogger(logSinks);
  IRIS_LOG_ENTER();

  if (sInitialized) {
    IRIS_LOG_LEAVE();
    return {Error::kAlreadyInitialized};
  }

  GOOGLE_PROTOBUF_VERIFY_VERSION;

  glslang::InitializeProcess();

  sTaskSchedulerInit.initialize();
  GetLogger()->debug("Default number of task threads: {}",
                     sTaskSchedulerInit.default_num_threads());

  ////
  // In order to reduce the verbosity of the Vulkan API, initialization occurs
  // over several sub-functions below. Each function is called in-order and
  // assumes the previous functions have all been called.
  ////

  absl::InlinedVector<gsl::czstring<>, 1> layerNames;
  if ((options & Options::kUseValidationLayers) ==
      Options::kUseValidationLayers) {
    layerNames.push_back("VK_LAYER_LUNARG_standard_validation");
  }

  // These are the extensions that we require from the instance.
  absl::InlinedVector<gsl::czstring<>, 10> instanceExtensionNames = {
    VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
    VK_KHR_SURFACE_EXTENSION_NAME, // surfaces are necessary for graphics
    VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME,
#if defined(VK_USE_PLATFORM_XCB_KHR) // plus the platform-specific surface
    VK_KHR_XCB_SURFACE_EXTENSION_NAME,
#elif defined(VK_USE_PLATFORM_WIN32_KHR) // plus the platform-specific surface
    VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#endif
  };

  if ((options & Options::kReportDebugMessages) ==
      Options::kReportDebugMessages) {
    instanceExtensionNames.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  }

  // These are the features that we require from the physical device.
  VkPhysicalDeviceFeatures2 physicalDeviceFeatures = {};
  physicalDeviceFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  physicalDeviceFeatures.features.fullDrawIndexUint32 = VK_TRUE;
  physicalDeviceFeatures.features.geometryShader = VK_TRUE;
  physicalDeviceFeatures.features.tessellationShader = VK_TRUE;
  physicalDeviceFeatures.features.depthClamp = VK_TRUE;
  physicalDeviceFeatures.features.fillModeNonSolid = VK_TRUE;
  physicalDeviceFeatures.features.wideLines = VK_TRUE;
  physicalDeviceFeatures.features.largePoints = VK_TRUE;
  physicalDeviceFeatures.features.multiViewport = VK_TRUE;
  physicalDeviceFeatures.features.pipelineStatisticsQuery = VK_TRUE;
  physicalDeviceFeatures.features.shaderTessellationAndGeometryPointSize =
    VK_TRUE;
  physicalDeviceFeatures.features.shaderUniformBufferArrayDynamicIndexing =
    VK_TRUE;
  physicalDeviceFeatures.features.shaderSampledImageArrayDynamicIndexing =
    VK_TRUE;
  physicalDeviceFeatures.features.shaderStorageBufferArrayDynamicIndexing =
    VK_TRUE;
  physicalDeviceFeatures.features.shaderStorageImageArrayDynamicIndexing =
    VK_TRUE;
  physicalDeviceFeatures.features.shaderClipDistance = VK_TRUE;
  physicalDeviceFeatures.features.shaderCullDistance = VK_TRUE;
  physicalDeviceFeatures.features.shaderFloat64 = VK_TRUE;
  physicalDeviceFeatures.features.shaderInt64 = VK_TRUE;

  // These are the extensions that we require from the physical device.
  char const* physicalDeviceExtensionNames[] = {
    VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME,
    VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME,
    VK_KHR_MAINTENANCE2_EXTENSION_NAME,
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
#if 0 // FIXME: which GPUs support this?
    VK_KHR_MULTIVIEW_EXTENSION_NAME
#endif
  };

#if PLATFORM_LINUX
  ::setenv(
    "VK_LAYER_PATH",
    absl::StrCat(iris::kVulkanSDKDirectory, "/etc/explicit_layer.d").c_str(),
    0);
#endif

  flextVkInit();

  if (auto instance =
        CreateInstance(appName, appVersion, instanceExtensionNames, layerNames,
                       (options & Options::kReportDebugMessages) ==
                         Options::kReportDebugMessages)) {
    sInstance = std::move(*instance);
  } else {
    IRIS_LOG_LEAVE();
    return instance.error();
  }

  flextVkInitInstance(sInstance); // initialize instance function pointers

  if ((options & Options::kReportDebugMessages) ==
      Options::kReportDebugMessages) {
    if (auto messenger = CreateDebugUtilsMessenger(sInstance)) {
      sDebugUtilsMessenger = std::move(*messenger);
    } else {
      GetLogger()->warn("Cannot create DebugUtilsMessenger: {}",
                        messenger.error().what());
    }
  }

  // FindDeviceGroup();

  if (auto physicalDevice = ChoosePhysicalDevice(
        sInstance, physicalDeviceFeatures, physicalDeviceExtensionNames,
        VK_QUEUE_GRAPHICS_BIT)) {
    sPhysicalDevice = std::move(*physicalDevice);
  } else {
    IRIS_LOG_LEAVE();
    return physicalDevice.error();
  }

  if (auto qfi = GetQueueFamilyIndex(sPhysicalDevice, VK_QUEUE_GRAPHICS_BIT)) {
    sGraphicsQueueFamilyIndex = *qfi;
  } else {
    IRIS_LOG_LEAVE();
    return qfi.error();
  }

  if (auto device =
        CreateDevice(sPhysicalDevice, physicalDeviceFeatures,
                     physicalDeviceExtensionNames, sGraphicsQueueFamilyIndex)) {
    sDevice = std::move(*device);
  } else {
    IRIS_LOG_LEAVE();
    return device.error();
  }

  vkGetDeviceQueue(sDevice, sGraphicsQueueFamilyIndex, 0, &sGraphicsCommandQueue);

  if (auto allocator = CreateAllocator(sPhysicalDevice, sDevice)) {
    sAllocator = std::move(*allocator);
  } else {
    IRIS_LOG_LEAVE();
    return allocator.error();
  }

#if 0
  if (auto error = CreateCommandPools(); error.code()) {
    IRIS_LOG_LEAVE();
    return {error};
  }

  if (auto error = CreateDescriptorPools(); error.code()) {
    IRIS_LOG_LEAVE();
    return {error};
  }

  if (auto error = CreateFencesAndSemaphores(); error.code()) {
    IRIS_LOG_LEAVE();
    return {error};
  }

  if (auto error = CreateRenderPass(); error.code()) {
    IRIS_LOG_LEAVE();
    return {error};
  }

  if (auto error = AllocateCommandBuffers(); error.code()) {
    IRIS_LOG_LEAVE();
    return {error};
  }

  if (auto error = CreateUniformBuffers(); error.code()) {
    IRIS_LOG_LEAVE();
    return {error};
  }

  if (auto error = CreateDescriptorSets(); error.code()) {
    IRIS_LOG_LEAVE();
    return {error};
  }
#endif
  sInitialized = true;
  sRunning = true;

  IRIS_LOG_LEAVE();
  return {Error::kNone};
} // iris::Renderer::Initialize

void iris::Renderer::Terminate() noexcept {
  IRIS_LOG_ENTER();
  sRunning = false;
  IRIS_LOG_LEAVE();
} // iris::Renderer::Terminate

bool iris::Renderer::IsRunning() noexcept {
  return sRunning;
} // iris::Renderer::IsRunning

void iris::Renderer::BeginFrame() noexcept {
  Expects(sInitialized == true);
  Expects(sRunning == true);

  auto currentTime = std::chrono::steady_clock::now();
  //auto const frameDelta =
    //std::chrono::duration<float>(currentTime - sPreviousFrameTime).count();
  sPreviousFrameTime = currentTime;

  decltype(sIOContinuations)::value_type ioContinuation;
  while (sIOContinuations.try_pop(ioContinuation)) {
    if (auto error = ioContinuation(); error.code()) {
      GetLogger()->error(error.what());
    }
  }
} // iris::Renderer::BeginFrame()

void iris::Renderer::EndFrame() noexcept {
  if (!sInitialized || !sRunning) return;

  sFrameNum += 1;
} // iris::Renderer::EndFrame

std::error_code
iris::Renderer::LoadFile(filesystem::path const& path) noexcept {
  IRIS_LOG_ENTER();

  class IOTask : public tbb::task {
  public:
    IOTask(filesystem::path p) noexcept(noexcept(std::move(p)))
      : path_(std::move(p)) {}

    tbb::task* execute() override {
      IRIS_LOG_ENTER();

      GetLogger()->debug("Loading {}", path_.string());
      auto const& ext = path_.extension();

      if (ext.compare(".json") == 0) {
        sIOContinuations.push(io::LoadJSON(path_));
      //} else if (ext.compare(".gltf") == 0) {
        //sIOContinuations.push(io::LoadGLTF(path_));
      } else {
        GetLogger()->error("Unhandled file extension '{}' for {}", ext.string(),
                           path_.string());
      }

      IRIS_LOG_LEAVE();
      return nullptr;
    }

  private:
    filesystem::path path_;
  }; // struct IOTask

  try {
    IOTask* ioTask = new (tbb::task::allocate_root()) IOTask(path);
    tbb::task::enqueue(*ioTask);
  } catch (std::exception const& e) {
    GetLogger()->error("Error enqueuing IO task for {}: {}", path.string(),
                       e.what());
    return {Error::kFileLoadFailed};
  }

  IRIS_LOG_LEAVE();
  return {Error::kNone};
} // LoadFile

std::error_code
iris::Renderer::Control(iris::Control::Control const& controlMessage) noexcept {
  IRIS_LOG_ENTER();

  if (!iris::Control::Control::Type_IsValid(controlMessage.type())) {
    GetLogger()->error("Invalid controlMessage message type {}",
                       controlMessage.type());
    IRIS_LOG_LEAVE();
    return Error::kControlMessageInvalid;
  }

  switch (controlMessage.type()) {

    //
    // FIXME: DRY
    //

  case iris::Control::Control_Type_DISPLAYS:
  #if 0
    for (int i = 0; i < controlMessage.displays().windows_size(); ++i) {
      auto&& windowMessage = controlMessage.displays().windows(i);
      auto const& bg = windowMessage.background_color();

      Window::Options options = Window::Options::kNone;
      if (windowMessage.show_system_decoration()) {
        options |= Window::Options::kDecorated;
      }
      if (windowMessage.is_stereo()) options |= Window::Options::kStereo;
      if (windowMessage.show_ui()) options |= Window::Options::kShowUI;

      if (auto win = Window::Create(
            windowMessage.name().c_str(),
            wsi::Offset2D{static_cast<std::int16_t>(windowMessage.x()),
                          static_cast<std::int16_t>(windowMessage.y())},
            wsi::Extent2D{static_cast<std::uint16_t>(windowMessage.width()),
                          static_cast<std::uint16_t>(windowMessage.height())},
            {bg.r(), bg.g(), bg.b(), bg.a()}, options,
            windowMessage.display())) {
        Windows().emplace(windowMessage.name(), std::move(*win));
      } else {
        GetLogger()->warn("Createing window failed: {}", win.error().what());
      }
    }
    #endif
    break;
  case iris::Control::Control_Type_WINDOW: {
    #if 0
    auto&& windowMessage = controlMessage.window();
    auto const& bg = windowMessage.background_color();

    Window::Options options = Window::Options::kNone;
    if (windowMessage.show_system_decoration()) {
      options |= Window::Options::kDecorated;
    }
    if (windowMessage.is_stereo()) options |= Window::Options::kStereo;
    if (windowMessage.show_ui()) options |= Window::Options::kShowUI;

    if (auto win = Window::Create(
          windowMessage.name().c_str(),
          wsi::Offset2D{static_cast<std::int16_t>(windowMessage.x()),
                        static_cast<std::int16_t>(windowMessage.y())},
          wsi::Extent2D{static_cast<std::uint16_t>(windowMessage.width()),
                        static_cast<std::uint16_t>(windowMessage.height())},
          {bg.r(), bg.g(), bg.b(), bg.a()}, options, windowMessage.display())) {
      Windows().emplace(windowMessage.name(), std::move(*win));
    } else {
      GetLogger()->warn("Creating window failed: {}", win.error().what());
    }
    #endif
  } break;
  default:
    GetLogger()->error("Unsupported controlMessage message type {}",
                       controlMessage.type());
    IRIS_LOG_LEAVE();
    return Error::kControlMessageInvalid;
    break;
  }

  IRIS_LOG_LEAVE();
  return Error::kNone;
} // iris::Renderer::Control
