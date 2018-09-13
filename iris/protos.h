#ifndef HEV_IRIS_PROTOS_H_
#define HEV_IRIS_PROTOS_H_

#include "iris/config.h"

#if PLATFORM_COMPILER_MSVC
#pragma warning(push)
#pragma warning(disable : 4100)
#elif PLATFORM_COMPILER_GCC
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

#include "iris/protos/control.pb.h"
#include "iris/protos/displays.pb.h"
#include "iris/protos/window.pb.h"

#if PLATFORM_COMPILER_MSVC
#pragma warning(pop)
#elif PLATFORM_COMPILER_GCC
#pragma GCC diagnostic pop
#endif

#endif
