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
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace iris::Renderer::io {

static std::atomic_bool sRunning{false};
static std::thread sThread;
static bool sInitialized{false};
static std::mutex sRequestsMutex{};
static std::condition_variable sRequestsReady{};
static std::deque<filesystem::path> sRequests{};
static std::mutex sResultsMutex{};
static std::vector<std::function<void(void)>> sResults{};

tl::expected<std::function<void(void)>, std::error_code>
LoadJSON(filesystem::path const& path) noexcept {
  IRIS_LOG_ENTER();
  std::string json;
  if (auto const& bytes = ReadFile(path)) {
    json = std::string(bytes->data(), bytes->size());
  } else {
    return tl::unexpected(bytes.error());
  }

  iris::Control::Control cMsg;
  if (auto status = google::protobuf::util::JsonStringToMessage(json, &cMsg);
      status.ok()) {
    IRIS_LOG_LEAVE();
    return [cMsg](){ Control(cMsg); };
  } else {
    GetLogger()->error("Cannot parse {}: {}", path.string(),
                       status.ToString());
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::make_error_code(std::errc::io_error));
  }
} // LoadJSON

tl::expected<std::function<void(void)>, std::error_code>
LoadGLTF(filesystem::path const& path) noexcept {
  IRIS_LOG_ENTER();
  GetLogger()->error("GLTF loading not implemented yet: {}", path.string());
  IRIS_LOG_LEAVE();
  return tl::unexpected(make_error_code(Error::kFileNotSupported));
} // LoadGLTF

static void HandleRequests() {
  IRIS_LOG_ENTER();

  while (sRunning) {
    filesystem::path path;

    try {
      std::unique_lock lock(sRequestsMutex);
      sRequestsReady.wait(lock, []{ return !sRequests.empty() || !sRunning; });
      if (!sRunning) break;

      path = sRequests.front();
      sRequests.pop_front();
    } catch (std::exception const& e) {
      GetLogger()->critical(
        "Exception encountered while getting IO request from queue: {}",
        e.what());
      std::terminate();
    }

    GetLogger()->debug("Loading {}", path.string());
    auto const& ext = path.extension();

    std::function<void(void)> result;

    if (ext.compare(".json") == 0) {
      if (auto r = LoadJSON(path)) result = std::move(*r);
    } else if (ext.compare(".gltf") == 0) {
      if (auto r = LoadGLTF(path)) result = std::move(*r);
    } else {
      GetLogger()->error("Unhandled file extension '{}' for {}", ext.string(),
                         path.string());
    }

    if (result) {
      std::unique_lock lock(sResultsMutex);
      sResults.push_back(std::move(result));
    }
  }

  IRIS_LOG_LEAVE();
} // HandleRequests

} // namespace iris::Renderer::io

std::error_code iris::Renderer::io::Initialize() noexcept {
  IRIS_LOG_ENTER();

  if (sInitialized) {
    IRIS_LOG_LEAVE();
    return Error::kAlreadyInitialized;
  }

  sRunning = true;

  try {
    sThread = std::thread(HandleRequests);
  } catch (std::system_error const& e) {
    GetLogger()->error("Exception encountered while starting IO thread: {}",
                       e.what());
    IRIS_LOG_LEAVE();
    return e.code();
  }

  sInitialized = true;

  IRIS_LOG_LEAVE();
  return Error::kNone;
} // iris::Renderer::io::Initialize()

std::error_code iris::Renderer::io::Shutdown() noexcept {
  IRIS_LOG_ENTER();
  sRunning = false;
  sRequestsReady.notify_one();

  try {
    sThread.join();
  } catch (std::system_error const& e) {
    GetLogger()->error("Exception encounted while trying to join IO thread: {}",
                       e.what());
    return e.code();
  }

  IRIS_LOG_LEAVE();
  return Error::kNone;
} // iris::Renderer::io::Shutdown

std::vector<std::function<void(void)>>
iris::Renderer::io::GetResults() noexcept {
  std::unique_lock lock(sResultsMutex);
  std::vector<std::function<void(void)>> results = sResults; // make a copy
  sResults.clear();
  return results;
} // iris::Renderer::io::GetResults

void iris::Renderer::io::LoadFile(filesystem::path path) noexcept {
  IRIS_LOG_ENTER();
  {
    std::lock_guard lock(sRequestsMutex);
    sRequests.push_back(path);
  }
  sRequestsReady.notify_one();
  IRIS_LOG_LEAVE();
} // iris::Renderer::LoadFile::io

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
} // iris::Renderer::io::ReadFile

