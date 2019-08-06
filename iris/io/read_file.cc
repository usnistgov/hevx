#include "io/read_file.h"
#include "config.h"
#include "logging.h"
#include <cstdio>
#include <exception>
#include <fcntl.h>
#include <memory>
#include <string>

iris::expected<std::vector<std::byte>, std::system_error>
iris::io::ReadFile(std::filesystem::path const& path) noexcept {
  IRIS_LOG_ENTER();

  std::unique_ptr<std::FILE, decltype(&std::fclose)> fh{nullptr, std::fclose};

  if (std::filesystem::exists(path)) {
    IRIS_LOG_DEBUG("Reading {}", path.string());
    fh.reset(std::fopen(path.string().c_str(), "rb"));
  } else {
    if (std::filesystem::exists(kIRISContentDirectory / path)) {
      IRIS_LOG_DEBUG("Reading {}", (kIRISContentDirectory / path).string());
      fh.reset(
        std::fopen((kIRISContentDirectory / path).string().c_str(), "rb"));
    }
  }

  if (!fh) {
    return unexpected(std::system_error(
      std::make_error_code(std::errc::no_such_file_or_directory),
      path.string()));
  }

  std::fseek(fh.get(), 0L, SEEK_END);
  std::vector<std::byte> bytes(std::ftell(fh.get()));

  IRIS_LOG_DEBUG("Reading {} bytes from {}", bytes.size(), path.string());
  std::fseek(fh.get(), 0L, SEEK_SET);

  std::size_t nRead =
    std::fread(bytes.data(), sizeof(std::byte), bytes.size(), fh.get());

  if ((std::ferror(fh.get()) && !std::feof(fh.get())) ||
      nRead != bytes.size()) {
    return unexpected(std::system_error(
      std::make_error_code(std::errc::io_error), path.string()));
  }

  IRIS_LOG_LEAVE();
  return bytes;
} // iris::io::ReadFile
