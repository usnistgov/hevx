#ifndef HEV_IRIS_LOGGING_H_
#define HEV_IRIS_LOGGING_H_

#include "iris/config.h"

#if PLATFORM_COMPILER_MSVC
#pragma warning(push)
#pragma warning(disable : 4127)
#endif
#include "spdlog/spdlog.h"
#if PLATFORM_COMPILER_MSVC
#pragma warning(pop)
#endif

namespace iris {

static spdlog::logger* GetLogger() noexcept {
  static std::shared_ptr<spdlog::logger> sLogger = spdlog::get("iris");
  return sLogger.get();
}

} // namespace iris::Renderer

#ifndef NDEBUG

//! \brief Logs entry into a function.
#define IRIS_LOG_ENTER() \
  GetLogger()->trace("ENTER: {} ({}:{})", __func__, __FILE__, __LINE__)
//! \brief Logs leave from a function.
#define IRIS_LOG_LEAVE() \
  GetLogger()->trace("LEAVE: {} ({}:{})", __func__, __FILE__, __LINE__)

#else

#define IRIS_LOG_ENTER()
#define IRIS_LOG_LEAVE()

#endif

#endif // HEV_IRIS_LOGGING_H_

