#include "renderer/io/gltf.h"
#include "error.h"
#include "logging.h"
#include "tao/json.hpp"

tl::expected<std::function<void(void)>, std::system_error>
iris::Renderer::io::LoadGLTF(filesystem::path const& path) noexcept {
  IRIS_LOG_ENTER();
  std::string json;
  if (auto const& bytes = ReadFile(path)) {
    json = std::string(bytes->data(), bytes->size());
  } else {
    return tl::unexpected(bytes.error());
  }

  tao::json::value const gltf = tao::json::from_string(json);

  GetLogger()->error("GLTF loading not implemented yet: {}", path.string());
  IRIS_LOG_LEAVE();
  return tl::unexpected(
    std::system_error(Error::kFileNotSupported, path.string()));
} // iris::Renderer::io::LoadGLTF

