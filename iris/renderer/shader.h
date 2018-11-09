#include "renderer.h"
#ifndef HEV_IRIS_RENDERER_SHADER_H_
#define HEV_IRIS_RENDERER_SHADER_H_

#include "tl/expected.hpp"
#include "renderer/vulkan.h"
#if STD_FS_IS_EXPERIMENTAL
#include <experimental/filesystem>
namespace filesystem = std::experimental::filesystem;
#else
#include <filesystem>
namespace filesystem = std::filesystem;
#endif
#include <string>
#include <string_view>
#include <system_error>

namespace iris::Renderer {

tl::expected<VkShaderModule, std::system_error>
CreateShaderFromSource(std::string_view source,
                       VkShaderStageFlagBits shaderStage,
                       std::string const& entry = "main") noexcept;

tl::expected<VkShaderModule, std::system_error>
CreateShaderFromFile(filesystem::path const& path,
                     VkShaderStageFlagBits shaderStage,
                     std::string const& entry = "main") noexcept;

} // namespace iris::Renderer

#endif // HEV_IRIS_RENDERER_SHADER_H_

