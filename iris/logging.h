#ifndef HEV_IRIS_LOGGING_H_
#define HEV_IRIS_LOGGING_H_

#include "config.h"

#if PLATFORM_COMPILER_MSVC
#pragma warning(push)
#pragma warning(disable : 4127)
#endif
#include "spdlog/spdlog.h"
#if PLATFORM_COMPILER_MSVC
#pragma warning(pop)
#endif

#if PLATFORM_COMPILER_GCC
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

namespace iris {

inline static spdlog::logger* GetLogger() noexcept {
  static std::shared_ptr<spdlog::logger> sLogger = spdlog::get("iris");
  return sLogger.get();
}

} // namespace iris::Renderer

#if PLATFORM_COMPILER_GCC
#pragma GCC diagnostic pop
#endif

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

#endif // HEV_IRIS_LOGGING_H_

