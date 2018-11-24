#include "renderer/io/io.h"
#include "renderer/impl.h"
#include "config.h"
#include "error.h"
#include "logging.h"
#include "renderer/io/gltf.h"
#include "renderer/io/json.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace iris::Renderer::io {

VkCommandPool sCommandPool{VK_NULL_HANDLE};
VkDescriptorPool sDescriptorPool{VK_NULL_HANDLE};

static std::atomic_bool sRunning{false};
static std::thread sThread;
static bool sInitialized{false};
static std::mutex sRequestsMutex{};
static std::condition_variable sRequestsReady{};
static std::deque<filesystem::path> sRequests{};
static std::mutex sResultsMutex{};
static std::vector<std::function<void(void)>> sResults{};

static void HandleRequests() {
  IRIS_LOG_ENTER();
  Expects(sCommandPool != VK_NULL_HANDLE);
  Expects(sDescriptorPool != VK_NULL_HANDLE);

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
      if (auto r = LoadJSON(path)) {
        result = std::move(*r);
      } else {
        GetLogger()->error("Error loading {}: {}", path.string(),
                           r.error().what());
      }
    } else if (ext.compare(".gltf") == 0) {
      if (auto r = LoadGLTF(path)) {
        result = std::move(*r);
      } else {
        GetLogger()->error("Error loading {}: {}", path.string(),
                           r.error().what());
      }
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

std::system_error iris::Renderer::io::Initialize() noexcept {
  IRIS_LOG_ENTER();

  if (sInitialized) {
    IRIS_LOG_LEAVE();
    return {Error::kAlreadyInitialized};
  }

  VkCommandPoolCreateInfo commandPoolCI = {};
  commandPoolCI.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  commandPoolCI.queueFamilyIndex = sGraphicsQueueFamilyIndex;

  if (auto result =
        vkCreateCommandPool(sDevice, &commandPoolCI, nullptr, &sCommandPool);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return {make_error_code(result), "Cannot create command pool"};
  }

  NameObject(VK_OBJECT_TYPE_COMMAND_POOL, sCommandPool, "io::sCommandPool");

  absl::FixedArray<VkDescriptorPoolSize> descriptorPoolSizes(11);
  descriptorPoolSizes[0] = { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 };
  descriptorPoolSizes[1] = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 };
  descriptorPoolSizes[2] = { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 };
  descriptorPoolSizes[3] = { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 };
  descriptorPoolSizes[4] = { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 };
  descriptorPoolSizes[5] = { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 };
  descriptorPoolSizes[6] = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 };
  descriptorPoolSizes[7] = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 };
  descriptorPoolSizes[8] = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 };
  descriptorPoolSizes[9] = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 };
  descriptorPoolSizes[10] = { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 };

  VkDescriptorPoolCreateInfo descriptorPoolCI = {};
  descriptorPoolCI.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  descriptorPoolCI.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  descriptorPoolCI.maxSets = 1000;
  descriptorPoolCI.poolSizeCount =
    static_cast<uint32_t>(descriptorPoolSizes.size());
  descriptorPoolCI.pPoolSizes = descriptorPoolSizes.data();

  if (auto result = vkCreateDescriptorPool(sDevice, &descriptorPoolCI, nullptr,
                                           &sDescriptorPool);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return {make_error_code(result), "Cannot create descriptor pool"};
  }

  NameObject(VK_OBJECT_TYPE_COMMAND_POOL, sDescriptorPool,
             "io::sDescriptorPool");

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
  return {Error::kNone};
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

  if (sDescriptorPool != VK_NULL_HANDLE) {
    vkDestroyDescriptorPool(sDevice, sDescriptorPool, nullptr);
  }

  if (sCommandPool != VK_NULL_HANDLE) {
    vkDestroyCommandPool(sDevice, sCommandPool, nullptr);
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

tl::expected<std::vector<std::byte>, std::system_error>
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
    return tl::unexpected(std::system_error(
      std::make_error_code(std::errc::no_such_file_or_directory),
      path.string()));
  }

  std::fseek(fh, 0L, SEEK_END);
  std::vector<std::byte> bytes(std::ftell(fh));
  GetLogger()->debug("Reading {} bytes from {}", bytes.size(), path.string());
  std::fseek(fh, 0L, SEEK_SET);
  std::fread(bytes.data(), sizeof(std::byte), bytes.size(), fh);

  if (std::ferror(fh) && !std::feof(fh)) {
    std::fclose(fh);
    return tl::unexpected(std::system_error(
      std::make_error_code(std::errc::io_error), path.string()));
  }

  std::fclose(fh);

  IRIS_LOG_LEAVE();
  return bytes;
} // iris::Renderer::io::ReadFile

