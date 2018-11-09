#include "renderer/shader.h"
#include "logging.h"
#include "renderer/impl.h"
#include "renderer/io.h"
#if PLATFORM_COMPILER_MSVC
#pragma warning(push)
#elif PLATFORM_COMPILER_GCC
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#endif
#include "shaderc/shaderc.hpp"
#if PLATFORM_COMPILER_MSVC
#pragma warning(pop)
#elif PLATFORM_COMPILER_GCC
#pragma GCC diagnostic pop
#endif

namespace iris::Renderer {

class ShaderIncluder : public shaderc::CompileOptions::IncluderInterface {
public:
  shaderc_include_result*
  GetInclude(char const* requested_source, shaderc_include_type type,
             char const* requesting_source,
             size_t include_depth[[maybe_unused]]) override;

  void ReleaseInclude(shaderc_include_result* data) override;

private:
  struct Include {
    filesystem::path path;
    std::string source;
    std::unique_ptr<shaderc_include_result> result;

    Include(filesystem::path p, std::string s)
      : path(std::move(p))
      , source(std::move(s))
      , result(new shaderc_include_result) {}
  }; // struct Include

  std::vector<Include> includes_{};
}; // class ShaderIncluder

shaderc_include_result* ShaderIncluder::GetInclude(
  char const* requested_source, shaderc_include_type type,
  char const* requesting_source, size_t include_depth[[maybe_unused]]) {
  IRIS_LOG_ENTER();
  filesystem::path path(requested_source);

  if (type == shaderc_include_type_relative) {
    filesystem::path parent(requesting_source);
    parent = parent.parent_path();
    path = parent / path;
  }

  try {
    if (!filesystem::exists(path)) { path.clear(); }
  } catch (...) { path.clear(); }

  if (!path.empty()) {
    if (auto s = io::ReadFile(path)) {
      includes_.push_back(Include(path, std::string(s->data(), s->size())));
    } else {
      includes_.push_back(Include(path, s.error().what()));
    }
  } else {
    includes_.push_back(Include(path, "file not found"));
  }

  Include& include = includes_.back();
  shaderc_include_result* result = include.result.get();

  result->source_name_length = include.path.string().size();
  result->source_name = include.path.string().c_str();
  result->content_length = include.source.size();
  result->content = include.source.data();
  result->user_data = nullptr;

  IRIS_LOG_LEAVE();
  return result;
} // ShaderIncluder::GetInclude

void ShaderIncluder::ReleaseInclude(shaderc_include_result* result) {
  IRIS_LOG_ENTER();
  for (std::size_t i = 0; i < includes_.size(); ++i) {
    if (includes_[i].result.get() == result) {
      includes_.erase(includes_.begin() + i);
      break;
    }
  }
  IRIS_LOG_LEAVE();
} // ShaderIncluder::ReleaseInclude

static tl::expected<std::vector<std::uint32_t>, std::string>
CompileShader(std::string_view source, VkShaderStageFlagBits shaderStage,
              filesystem::path const& path, std::string const& entryPoint) {
  IRIS_LOG_ENTER();
  shaderc::Compiler compiler;
  shaderc::CompileOptions options;
  options.SetOptimizationLevel(shaderc_optimization_level_performance);
  options.SetIncluder(std::make_unique<ShaderIncluder>());

  auto const kind = [&shaderStage]() {
    if ((shaderStage & VK_SHADER_STAGE_VERTEX_BIT)) {
      return shaderc_vertex_shader;
    } else if ((shaderStage & VK_SHADER_STAGE_FRAGMENT_BIT)) {
      return shaderc_fragment_shader;
    } else {
      GetLogger()->critical("Unhandled shaderStage: {}", shaderStage);
      std::terminate();
    }
  }();

  auto spv = compiler.CompileGlslToSpv(source.data(), source.size(), kind,
                                       path.string().c_str(),
                                       entryPoint.c_str(), options);
  if (spv.GetCompilationStatus() != shaderc_compilation_status_success) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(spv.GetErrorMessage());
  }

  std::vector<std::uint32_t> code;
  std::copy(std::begin(spv), std::end(spv), std::back_inserter(code));

  IRIS_LOG_LEAVE();
  return code;
} // CompileShader

} // namespace iris::Renderer

tl::expected<VkShaderModule, std::system_error>
iris::Renderer::CreateShaderFromSource(std::string_view source,
                                       VkShaderStageFlagBits shaderStage,
                                       std::string const& entry) noexcept {
  IRIS_LOG_ENTER();
  VkResult result;

  if (auto code = CompileShader(source, shaderStage, "", entry)) {
    VkShaderModuleCreateInfo smci = {};
    smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    // codeSize is bytes, not count of words
    smci.codeSize = gsl::narrow_cast<std::uint32_t>(code->size() * 4);
    smci.pCode = code->data();

    VkShaderModule module;
    result = vkCreateShaderModule(sDevice, &smci, nullptr, &module);
    if (result != VK_SUCCESS) {
      IRIS_LOG_LEAVE();
      return tl::unexpected(std::system_error(make_error_code(result),
                                              "Cannot create shader module"));
    }

    IRIS_LOG_LEAVE();
    return module;
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(Error::kShaderCompileFailed, code.error()));
  }
} // iris::Renderer::CreateShaderFromSource
#if 0
tl::expected<VkShaderModule, std::error_code>
iris::Renderer::CreateShaderFromFile(filesystem::path const& path,
                                     VkShaderStageFlagBits shaderStage,
                                     std::string const& entry) noexcept {
  IRIS_LOG_ENTER();
  VkResult result;

  std::vector<char> source;
  if (auto s = io::ReadFile(path)) {
    source = std::move(*s);
  } else {
    return tl::unexpected(s.error());
  }

  if (auto code = CompileShader({source.data(), source.size()}, shaderStage,
                                path, entry)) {
    VkShaderModuleCreateInfo smci = {};
    smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    // codeSize is bytes, not count of words
    smci.codeSize = gsl::narrow_cast<std::uint32_t>(code->size() * 4);
    smci.pCode = code->data();

    VkShaderModule module;
    result = vkCreateShaderModule(sDevice, &smci, nullptr, &module);
    if (result != VK_SUCCESS) {
      GetLogger()->error("Cannot create shader module: {}", to_string(result));
      IRIS_LOG_LEAVE();
      return tl::unexpected(make_error_code(result));
    }

    IRIS_LOG_LEAVE();
    return module;
  } else {
    GetLogger()->error("Cannot compile shader: {}", code.error());
    IRIS_LOG_LEAVE();
    return tl::unexpected(Error::kShaderCompileFailed);
  }
} // iris::Renderer::CreateShaderFromFile
#endif
