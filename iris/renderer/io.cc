#include "renderer/io.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "config.h"
#include "error.h"
#include "logging.h"
#include "protos.h"
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

std::error_code
iris::Renderer::io::LoadFile(std::string_view fileName) noexcept {
  IRIS_LOG_ENTER();

  GetLogger()->debug("Loading {}", fileName);
  std::vector<std::string_view> parts = absl::StrSplit(fileName, ".");

  if (parts.back() == "json") {
    std::string fn(fileName);
    std::FILE* fh = std::fopen(fn.c_str(), "rb");
    if (!fh) {
      fn = absl::StrCat(kIRISContentDirectory, "/", fileName);
      GetLogger()->debug("Loading {} failed, trying {}", fileName, fn);
      fh = std::fopen(fn.c_str(), "rb");
    }

    if (!fh) {
      GetLogger()->error("Unable to open or find {}: {}", fileName,
                         strerror(errno));
      return Error::kFileNotSupported;
    }

    std::fseek(fh, 0L, SEEK_END);
    std::vector<char> bytes(std::ftell(fh));
    std::fseek(fh, 0L, SEEK_SET);
    std::fread(bytes.data(), sizeof(char), bytes.size(), fh);

    if (std::ferror(fh) && !std::feof(fh)) {
      std::fclose(fh);
      GetLogger()->error("Unable to read {}: {}", fileName, strerror(errno));
      return Error::kFileNotSupported;
    }

    std::fclose(fh);
    std::string json(bytes.data(), bytes.size());

    iris::Control::Control controlMessage;
    if (auto status =
          google::protobuf::util::JsonStringToMessage(json, &controlMessage);
        !status.ok()) {
      GetLogger()->error("Unable to parse {}: {}", fileName, status.ToString());
      IRIS_LOG_LEAVE();
      return Error::kFileNotSupported;
    } else {
      IRIS_LOG_LEAVE();
      return Control(controlMessage);
    }
  } else {
    GetLogger()->error("Unhandled file extension: {} for {}", parts.back(),
                       fileName);
    IRIS_LOG_LEAVE();
    return Error::kFileNotSupported;
  }
} // iris::Renderer::LoadFile::io

