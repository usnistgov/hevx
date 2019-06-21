#ifndef HEV_IRIS_LOGGING_H_
#define HEV_IRIS_LOGGING_H_

#include "iris/config.h"

#if PLATFORM_COMPILER_MSVC
#include <codeanalysis/warnings.h>
#pragma warning(push)
#pragma warning(disable: ALL_CODE_ANALYSIS_WARNINGS)
#pragma warning(disable: ALL_CPPCORECHECK_WARNINGS)
#endif

#include "spdlog/spdlog.h"

#if PLATFORM_COMPILER_MSVC
#pragma warning(pop)
#endif

namespace iris {

inline static spdlog::logger* GetLogger() noexcept {
  static std::shared_ptr<spdlog::logger> sLogger = spdlog::get("iris");
  return sLogger.get();
}

} // namespace iris

#if PLATFORM_COMPILER_GCC
#pragma GCC diagnostic pop
#endif

#define IRIS_LOG_CRITICAL(fmt, ...)                                            \
  ::iris::GetLogger()->critical(fmt " ({}:{})", ##__VA_ARGS__, __FILE__,       \
                                __LINE__)

#define IRIS_LOG_ERROR(fmt, ...)                                               \
  ::iris::GetLogger()->error(fmt " ({}:{})", ##__VA_ARGS__, __FILE__, __LINE__)

#define IRIS_LOG_WARN(fmt, ...)                                                \
  ::iris::GetLogger()->warn(fmt " ({}:{})", ##__VA_ARGS__, __FILE__, __LINE__)

#define IRIS_LOG_INFO(fmt, ...)                                                \
  ::iris::GetLogger()->info(fmt " ({}:{})", ##__VA_ARGS__, __FILE__, __LINE__)

#define IRIS_LOG_DEBUG(fmt, ...)                                               \
  ::iris::GetLogger()->debug(fmt " ({}:{})", ##__VA_ARGS__, __FILE__, __LINE__)

#define IRIS_LOG_TRACE(fmt, ...)                                               \
  ::iris::GetLogger()->trace(fmt " ({}:{})", ##__VA_ARGS__, __FILE__, __LINE__)

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

#endif // HEV_IRIS_LOGGING_H_

