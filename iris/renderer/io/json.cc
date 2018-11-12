#include "renderer/io/json.h"
#include "renderer/impl.h"
#include "error.h"
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

tl::expected<std::function<void(void)>, std::system_error>
iris::Renderer::io::LoadJSON(filesystem::path const& path) noexcept {
  IRIS_LOG_ENTER();
  std::string json;
  if (auto&& bytes = ReadFile(path)) {
    json =
      std::string(reinterpret_cast<char const*>(bytes->data()), bytes->size());
  } else {
    return tl::unexpected(bytes.error());
  }

  iris::Control::Control cMsg;
  if (auto status = google::protobuf::util::JsonStringToMessage(json, &cMsg);
      status.ok()) {
    IRIS_LOG_LEAVE();
    return [cMsg](){ Control(cMsg); };
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(Error::kFileParseFailed, status.ToString()));
  }
} // iris::Renderer::io::LoadJSON

