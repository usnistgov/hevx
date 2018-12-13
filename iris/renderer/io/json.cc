#include "renderer/io/json.h"
#include "config.h"
#include "error.h"
#include "renderer/io/read_file.h"
#include "renderer/renderer.h"
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

std::function<std::system_error(void)>
iris::Renderer::io::LoadJSON(filesystem::path const& path) noexcept {
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
    return [cMsg]() { return std::system_error(Control(cMsg)); };
  } else {
    IRIS_LOG_LEAVE();
    return [message = status.ToString()]() {
      return std::system_error(Error::kFileParseFailed, message);
    };
  }
} // iris::Renderer::io::LoadJSON

