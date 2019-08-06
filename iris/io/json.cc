#include "io/json.h"
#include "config.h"
#include "error.h"
#include "io/read_file.h"
#if PLATFORM_COMPILER_MSVC
#pragma warning(push)
#pragma warning(disable : 4100)
#elif PLATFORM_COMPILER_GCC
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#include "google/protobuf/util/json_util.h"
#if PLATFORM_COMPILER_MSVC
#pragma warning(pop)
#elif PLATFORM_COMPILER_GCC
#pragma GCC diagnostic pop
#endif
#include "logging.h"
#include "protos.h"
#include "renderer.h"
#include "types.h"
#include <string>
#include <vector>

std::function<std::system_error(void)>
iris::io::LoadJSON(std::filesystem::path const& path) noexcept {
  IRIS_LOG_ENTER();

  std::string json;
  if (auto&& bytes = ReadFile(path)) {
    json =
      std::string(reinterpret_cast<char const*>(bytes->data()), bytes->size());
  } else {
    IRIS_LOG_LEAVE();
    return [error = bytes.error()]() { return error; };
  }

  iris::Control::Control cMsg;
  if (auto status = google::protobuf::util::JsonStringToMessage(json, &cMsg);
      status.ok()) {
    IRIS_LOG_LEAVE();
    return [cMsg]() {
      if (auto result = Renderer::ProcessControlMessage(cMsg)) {
        return std::system_error(Error::kNone);
      } else {
        return result.error();
      }
    };
  } else {
    IRIS_LOG_LEAVE();
    return [message = status.ToString()]() {
      return std::system_error(Error::kFileParseFailed, message);
    };
  }
} // iris::io::LoadJSON
