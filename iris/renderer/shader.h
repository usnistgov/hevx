#ifndef HEV_IRIS_RENDERER_SHADER_H_
#define HEV_IRIS_RENDERER_SHADER_H_

#include "iris/renderer/impl.h"
#if STD_FS_IS_EXPERIMENTAL
#include <experimental/filesystem>
namespace filesystem = std::experimental::filesystem;
#else
#include <filesystem>
namespace filesystem = std::filesystem;
#endif
#include <string>
#include <string_view>

namespace iris::Renderer {

struct Shader {
  static tl::expected<Shader, std::system_error>
  CreateFromSource(std::string_view source, VkShaderStageFlagBits stage,
                   std::string entry = "main", std::string name = {}) noexcept;

  VkShaderStageFlagBits stage;
  VkShaderModule handle;
  std::string entry;

  operator VkShaderModule() const noexcept { return handle; }

  Shader() = default;
  Shader(Shader const&) = delete;
  Shader(Shader&& other) noexcept;
  Shader& operator=(Shader const&) = delete;
  Shader& operator=(Shader&& rhs) noexcept;
  ~Shader() noexcept;

private:
  std::string name;
}; // struct Shader

} // namespace iris::Renderer

#endif // HEV_IRIS_RENDERER_SHADER_H_

