#ifndef HEV_IRIS_LOGGING_H_
#define HEV_IRIS_LOGGING_H_

#if PLATFORM_COMPILER_MSVC
#pragma warning(push)
#pragma warning(disable : 4127)
#endif
#include "spdlog/spdlog.h"
#if PLATFORM_COMPILER_MSVC
#pragma warning(pop)
#endif

namespace iris {

static spdlog::logger* sGetLogger() noexcept {
  static std::shared_ptr<spdlog::logger> sLogger = spdlog::get("iris");
  return sLogger.get();
}

} // namespace iris::Renderer

#endif // HEV_IRIS_LOGGING_H_

