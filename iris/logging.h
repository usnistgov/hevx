#ifndef HEV_IRIS_LOGGING_H_
#define HEV_IRIS_LOGGING_H_

#include "iris/config.h"

#include "spdlog/spdlog.h"

namespace iris {

inline static spdlog::logger* GetLogger() noexcept {
  static std::shared_ptr<spdlog::logger> sLogger = spdlog::get("iris");
  return sLogger.get();
}

} // namespace iris

#if PLATFORM_COMPILER_GCC
#pragma GCC diagnostic pop
#endif

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

