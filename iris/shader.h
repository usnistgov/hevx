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

struct ShaderGroup {
  VkRayTracingShaderGroupTypeNV type;
  std::uint32_t generalShaderIndex{VK_SHADER_UNUSED_NV};
  std::uint32_t closestHitShaderIndex{VK_SHADER_UNUSED_NV};
  std::uint32_t anyHitShaderIndex{VK_SHADER_UNUSED_NV};
  std::uint32_t intersectionShaderIndex{VK_SHADER_UNUSED_NV};

  static ShaderGroup General(std::uint32_t index) {
    return {VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_NV, index,
            VK_SHADER_UNUSED_NV, VK_SHADER_UNUSED_NV, VK_SHADER_UNUSED_NV};
  }

  static ShaderGroup
  ProceduralHit(std::uint32_t intersectionIndex, std::uint32_t closestHitIndex,
                std::uint32_t anyHitIndex = VK_SHADER_UNUSED_NV) {
    return {VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_NV,
            VK_SHADER_UNUSED_NV, closestHitIndex, anyHitIndex,
            intersectionIndex};
  }
}; // struct ShaderGroup

[[nodiscard]] tl::expected<Shader, std::system_error>
CompileShaderFromSource(std::string_view source,
                        VkShaderStageFlagBits stage) noexcept;

[[nodiscard]] tl::expected<Shader, std::system_error>
LoadShaderFromFile(filesystem::path const& path,
                   VkShaderStageFlagBits stage) noexcept;

} // namespace iris

#endif // HEV_IRIS_SHADER_H_

