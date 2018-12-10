#include "renderer/shader.h"
#include "logging.h"
#include "renderer/impl.h"
#include "renderer/io/read_file.h"
#include "glslang/Public/ShaderLang.h"

namespace iris::Renderer {

[[nodiscard]] static tl::expected<std::vector<std::uint32_t>, std::string>
CompileShaderFromSource(std::string_view source,
                        VkShaderStageFlagBits shaderStage[[maybe_unused]],
                        filesystem::path const& path[[maybe_unused]],
                        gsl::span<std::string> macroDefinitions[[maybe_unused]],
                        std::string const& entryPoint[[maybe_unused]]) {
  IRIS_LOG_ENTER();
  Expects(source.size() > 0);
  IRIS_LOG_LEAVE();
  return tl::unexpected(std::string("not implemented"));
#if 0
  shaderc::Compiler compiler;
  shaderc::CompileOptions options;
  options.SetOptimizationLevel(shaderc_optimization_level_performance);
  options.SetIncluder(std::make_unique<ShaderIncluder>());

  for (auto&& macro : macroDefinitions) options.AddMacroDefinition(macro);

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
    return tl::unexpected("\n" + spv.GetErrorMessage());
  }

  std::vector<std::uint32_t> code;
  std::copy(std::begin(spv), std::end(spv), std::back_inserter(code));

  Ensures(code.size() > 0);
  IRIS_LOG_LEAVE();
  return code;
#endif
} // CompileShaderFromSource

} // namespace iris::Renderer

tl::expected<iris::Renderer::Shader, std::system_error>
iris::Renderer::Shader::CreateFromSource(
  std::string_view source, VkShaderStageFlagBits stage,
  gsl::span<std::string> macroDefinitions, std::string entry,
  std::string name) noexcept {
  IRIS_LOG_ENTER();
  Expects(sDevice != VK_NULL_HANDLE);
  Expects(source.size() > 0);

  Shader shader;

  std::vector<std::uint32_t> code;
  if (auto c = CompileShaderFromSource(source, stage, "<inline>",
                                       macroDefinitions, entry)) {
    code = std::move(*c);
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(Error::kShaderCompileFailed, c.error()));
  }

  VkShaderModuleCreateInfo smci = {};
  smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  // codeSize is in bytes, not count of words (which is what vector.size() is)
  smci.codeSize = gsl::narrow_cast<std::uint32_t>(code.size() * 4);
  smci.pCode = code.data();

  if (auto result =
        vkCreateShaderModule(sDevice, &smci, nullptr, &shader.handle);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(make_error_code(result),
                                            "Cannot create shader module"));
  }

  if (!name.empty()) {
    NameObject(VK_OBJECT_TYPE_SHADER_MODULE, shader.handle, name.c_str());
  }

  shader.stage = stage;
  shader.entry = std::move(entry);
  shader.name = std::move(name);

  Ensures(shader.handle != VK_NULL_HANDLE);
  IRIS_LOG_LEAVE();
  return std::move(shader);
} // iris::Renderer::Shader::CreateFromSource

tl::expected<iris::Renderer::Shader, std::system_error>
iris::Renderer::Shader::CreateFromFile(filesystem::path const& path,
                                       VkShaderStageFlagBits stage,
                                       gsl::span<std::string> macroDefinitions,
                                       std::string entry,
                                       std::string name) noexcept {
  IRIS_LOG_ENTER();
  Expects(sDevice != VK_NULL_HANDLE);
  Expects(!path.empty());

  Shader shader;

  std::vector<std::byte> source;
  if (auto s = io::ReadFile(path)) {
    source = std::move(*s);
  } else {
    return tl::unexpected(s.error());
  }

  std::vector<std::uint32_t> code;
  if (auto c = CompileShaderFromSource(
        {reinterpret_cast<char*>(source.data()), source.size()}, stage, path,
        macroDefinitions, entry)) {
    code = std::move(*c);
  } else {
    IRIS_LOG_LEAVE();
    return tl::unexpected(
      std::system_error(Error::kShaderCompileFailed, c.error()));
  }

  VkShaderModuleCreateInfo smci = {};
  smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  // codeSize is in bytes, not count of words (which is what vector.size() is)
  smci.codeSize = gsl::narrow_cast<std::uint32_t>(code.size() * 4);
  smci.pCode = code.data();

  if (auto result =
        vkCreateShaderModule(sDevice, &smci, nullptr, &shader.handle);
      result != VK_SUCCESS) {
    IRIS_LOG_LEAVE();
    return tl::unexpected(std::system_error(make_error_code(result),
                                            "Cannot create shader module"));
  }

  if (!name.empty()) {
    NameObject(VK_OBJECT_TYPE_SHADER_MODULE, shader.handle, name.c_str());
  }

  shader.stage = stage;
  shader.entry = std::move(entry);
  shader.name = std::move(name);

  Ensures(shader.handle != VK_NULL_HANDLE);
  IRIS_LOG_LEAVE();
  return std::move(shader);
} // iris::Renderer::Shader::CreateFromFile

iris::Renderer::Shader::Shader(Shader&& other) noexcept
  : stage(other.stage)
  , handle(other.handle)
  , entry(std::move(other.entry))
  , name(std::move(other.name)) {
  other.handle = nullptr;
} // iris::Renderer::Shader::Shader

iris::Renderer::Shader& iris::Renderer::Shader::operator=(Shader&& rhs) noexcept {
  if (this == &rhs) return *this;

  stage = rhs.stage;
  handle = rhs.handle;
  entry = std::move(rhs.entry);
  name = std::move(rhs.name);

  rhs.handle = VK_NULL_HANDLE;

  return *this;
} // iris::Renderer::Shader::operator=

iris::Renderer::Shader::~Shader() noexcept {
  if (handle == VK_NULL_HANDLE) return;
  IRIS_LOG_ENTER();
  Expects(sDevice != VK_NULL_HANDLE);

  vkDestroyShaderModule(sDevice, handle, nullptr);

  IRIS_LOG_LEAVE();
} // iris::Renderer::~Shader

