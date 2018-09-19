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
  LoadFileTask(std::experimental::filesystem::path path)
    : path_(std::move(path)) {}

private:
  std::experimental::filesystem::path path_;

  TaskResult load() noexcept;
  tbb::task* execute() override;
}; // class LoadFileTask

TaskResult LoadFileTask::load() noexcept {
  IRIS_LOG_ENTER();

  GetLogger()->debug("Loading {}", path_.c_str());
  auto const& ext = path_.extension();

  if (ext.compare(".json") == 0) {
    std::string json;
    if (auto const& bytes = ReadFile(path_)) {
      json = std::string(bytes->data(), bytes->size());
    } else {
      return bytes.error();
    }

    iris::Control::Control controlMessage;
    if (auto status =
          google::protobuf::util::JsonStringToMessage(json, &controlMessage);
        !status.ok()) {
      GetLogger()->error("Unable to parse {}: {}", path_.c_str(),
                         status.ToString());
      IRIS_LOG_LEAVE();
      return std::make_error_code(std::errc::io_error);
    } else {
      IRIS_LOG_LEAVE();
      return controlMessage;
    }
  } else {
    GetLogger()->error("Unhandled file extension '{}' for {}", ext.c_str(),
                       path_.c_str());
    IRIS_LOG_LEAVE();
    return make_error_code(Error::kFileNotSupported);
  }
} // LoadFileTask::load

tbb::task* LoadFileTask::execute() {
  IRIS_LOG_ENTER();
  sTasksResultsQueue.push(load());
  IRIS_LOG_LEAVE();
  return nullptr;
} // LoadFileTask::execute

} // namespace iris::Renderer::io

tl::expected<std::vector<char>, std::error_code> iris::Renderer::io::ReadFile(
  std::experimental::filesystem::path path) noexcept {
  IRIS_LOG_ENTER();

  GetLogger()->debug("Reading {}", path.c_str());

  std::FILE* fh = std::fopen(path.c_str(), "rb");

  if (!fh) {
    path = std::experimental::filesystem::path(kIRISContentDirectory) / path;
    GetLogger()->debug("Reading failed trying {}", path.c_str());
    fh = std::fopen(path.c_str(), "rb");
  }

  if (!fh) {
    tl::unexpected(std::make_error_code(std::errc::no_such_file_or_directory));
  }

  std::fseek(fh, 0L, SEEK_END);
  std::vector<char> bytes(std::ftell(fh));
  std::fseek(fh, 0L, SEEK_SET);
  std::fread(bytes.data(), sizeof(char), bytes.size(), fh);

  if (std::ferror(fh) && !std::feof(fh)) {
    std::fclose(fh);
    return tl::unexpected(std::make_error_code(std::errc::io_error));
  }

  std::fclose(fh);

  IRIS_LOG_LEAVE();
  return bytes;
}

void iris::Renderer::io::LoadFile(
  std::experimental::filesystem::path path) noexcept {
  IRIS_LOG_ENTER();
  LoadFileTask* task =
    new (tbb::task::allocate_root()) LoadFileTask(std::move(path));
  tbb::task::enqueue(*task);
  IRIS_LOG_LEAVE();
} // iris::Renderer::LoadFile::io

