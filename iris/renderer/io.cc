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
  LoadFileTask(filesystem::path path)
    : path_(std::move(path)) {}

private:
  filesystem::path path_;

  tbb::task* execute() override {
    IRIS_LOG_ENTER();
    sTasksResultsQueue.push(Load());
    IRIS_LOG_LEAVE();
    return nullptr;
  }

  TaskResult Load() noexcept;
  TaskResult LoadJSON() noexcept;
  TaskResult LoadGLTF() noexcept;
}; // class LoadFileTask

TaskResult LoadFileTask::Load() noexcept {
  IRIS_LOG_ENTER();

  GetLogger()->debug("Loading {}", path_.string());
  auto const& ext = path_.extension();

  if (ext.compare(".json") == 0) {
    return LoadJSON();
  } else if (ext.compare(".gltf") == 0) {
    return LoadGLTF();
  } else {
    GetLogger()->error("Unhandled file extension '{}' for {}", ext.string(),
                       path_.string());
    IRIS_LOG_LEAVE();
    return make_error_code(Error::kFileNotSupported);
  }
} // LoadFileTask::Load

TaskResult LoadFileTask::LoadJSON() noexcept {
  IRIS_LOG_ENTER();
  std::string json;
  if (auto const& bytes = ReadFile(path_)) {
    json = std::string(bytes->data(), bytes->size());
  } else {
    return bytes.error();
  }

  iris::Control::Control cMsg;
  if (auto status = google::protobuf::util::JsonStringToMessage(json, &cMsg);
      status.ok()) {
    IRIS_LOG_LEAVE();
    return cMsg;
  } else {
    GetLogger()->error("Cannot parse {}: {}", path_.string(),
                       status.ToString());
    IRIS_LOG_LEAVE();
    return std::make_error_code(std::errc::io_error);
  }
} // LoadFileTask::LoadJSON

TaskResult LoadFileTask::LoadGLTF() noexcept {
  IRIS_LOG_ENTER();
  IRIS_LOG_LEAVE();
  return make_error_code(Error::kFileNotSupported);
} // LoadFileTask::LoadGLTF

} // namespace iris::Renderer::io

tl::expected<std::vector<char>, std::error_code>
iris::Renderer::io::ReadFile(filesystem::path path) noexcept {
  IRIS_LOG_ENTER();

  GetLogger()->debug("Reading {}", path.string());

  std::FILE* fh = std::fopen(path.string().c_str(), "rb");

  if (!fh) {
    path = filesystem::path(kIRISContentDirectory) / path;
    GetLogger()->debug("Reading failed trying {}", path.string());
    fh = std::fopen(path.string().c_str(), "rb");
  }

  if (!fh) {
    GetLogger()->debug("Reading {} failed", path.string());
    return tl::unexpected(
      std::make_error_code(std::errc::no_such_file_or_directory));
  }

  std::fseek(fh, 0L, SEEK_END);
  std::vector<char> bytes(std::ftell(fh));
  GetLogger()->debug("Reading {} bytes from {}", bytes.size(), path.string());
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

void iris::Renderer::io::LoadFile(filesystem::path path) noexcept {
  IRIS_LOG_ENTER();
  LoadFileTask* task =
    new (tbb::task::allocate_root()) LoadFileTask(std::move(path));
  tbb::task::enqueue(*task);
  IRIS_LOG_LEAVE();
} // iris::Renderer::LoadFile::io
