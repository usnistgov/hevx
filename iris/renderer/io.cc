#include "renderer/io.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "config.h"
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
#include "renderer/impl.h"
#include "renderer/renderer.h"
#include "tasks.h"
#include "tbb/task.h"
#include <memory>

namespace iris::Renderer::io {

class LoadFileTask : public tbb::task {
public:
  LoadFileTask(std::string_view fileName)
    : fileName_(fileName) {}

private:
  std::string fileName_;

  TaskResult load() noexcept;
  tbb::task* execute() override;
}; // class LoadFileTask

TaskResult LoadFileTask::load() noexcept {
  IRIS_LOG_ENTER();

  GetLogger()->debug("Loading {}", fileName_);
  std::vector<std::string_view> parts = absl::StrSplit(fileName_, ".");

  if (parts.back() == "json") {
    std::string fn(fileName_);
    std::FILE* fh = std::fopen(fn.c_str(), "rb");

    if (!fh) {
      fn = absl::StrCat(kIRISContentDirectory, "/", fileName_);
      GetLogger()->debug("Loading {} failed, trying {}", fileName_, fn);
      fh = std::fopen(fn.c_str(), "rb");
    }

    if (!fh) return std::make_error_code(std::errc::no_such_file_or_directory);

    std::fseek(fh, 0L, SEEK_END);
    std::vector<char> bytes(std::ftell(fh));
    std::fseek(fh, 0L, SEEK_SET);
    std::fread(bytes.data(), sizeof(char), bytes.size(), fh);

    if (std::ferror(fh) && !std::feof(fh)) {
      std::fclose(fh);
      return std::make_error_code(std::errc::io_error);
    }

    std::fclose(fh);
    std::string json(bytes.data(), bytes.size());

    iris::Control::Control controlMessage;
    if (auto status =
          google::protobuf::util::JsonStringToMessage(json, &controlMessage);
        !status.ok()) {
      GetLogger()->error("Unable to parse {}: {}", fileName_,
                         status.ToString());
      IRIS_LOG_LEAVE();
      return std::make_error_code(std::errc::io_error);
    } else {
      IRIS_LOG_LEAVE();
      return Control(controlMessage);
    }
  } else {
    GetLogger()->error("Unhandled file extension: {} for {}", parts.back(),
                       fileName_);
    IRIS_LOG_LEAVE();
    return make_error_code(Error::kFileNotSupported);
  }
} // LoadFileTask::load

tbb::task* LoadFileTask::execute() {
  IRIS_LOG_ENTER();
  sTasksResultsQueue.push(load());
  return nullptr;
  IRIS_LOG_LEAVE();
} // LoadFileTask::execute

} // namespace iris::Renderer::io

void iris::Renderer::io::LoadFile(std::string_view fileName) noexcept {
  IRIS_LOG_ENTER();
  LoadFileTask* task = new (tbb::task::allocate_root()) LoadFileTask(fileName);
  tbb::task::enqueue(*task);
  IRIS_LOG_LEAVE();
} // iris::Renderer::LoadFile::io

