#ifndef HEV_IRIS_SHADER_H_
#define HEV_IRIS_SHADER_H_

#include "expected.hpp"
#if STD_FS_IS_EXPERIMENTAL
#include <experimental/filesystem>
namespace filesystem = std::experimental::filesystem;
#else
#include <filesystem>
namespace filesystem = std::filesystem;
#endif
#include "iris/vulkan.h"
#include <system_error>

namespace iris {

struct Shader {
  VkShaderModule module{VK_NULL_HANDLE};
  VkShaderStageFlagBits stage{VK_SHADER_STAGE_ALL};
}; // struct Shader

[[nodiscard]] tl::expected<Shader, std::system_error>
CompileShaderFromSource(std::string_view source,
                        VkShaderStageFlagBits stage) noexcept;

[[nodiscard]] tl::expected<Shader, std::system_error>
LoadShaderFromFile(filesystem::path const& path,
                   VkShaderStageFlagBits stage) noexcept;

} // namespace iris

#endif // HEV_IRIS_SHADER_H_

