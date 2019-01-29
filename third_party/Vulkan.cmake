set(_git_tag sdk-1.1.97.0)

fetch_sources("Vulkan_ValidationLayers"
  "https://github.com/KhronosGroup/Vulkan-ValidationLayers" "${_git_tag}")
set(Vulkan_ValidationLayers_SOURCE_DIR
  ${CMAKE_CURRENT_BINARY_DIR}/Vulkan_ValidationLayers)
set(Vulkan_ValidationLayers_BINARY_DIR
  ${CMAKE_CURRENT_BINARY_DIR}/Vulkan_ValidationLayers-build)

file(STRINGS ${Vulkan_ValidationLayers_SOURCE_DIR}/scripts/known_good.json
  known_good NEWLINE_CONSUME)
sbeParseJson(kg known_good)

foreach(repo ${kg.repos})
  string(REPLACE "-" "_" _name ${kg.repos_${repo}.name})
  fetch_sources(${_name} ${kg.repos_${repo}.url} ${kg.repos_${repo}.commit})
  set(${_name}_SOURCE_DIR ${CMAKE_CURRENT_BINARY_DIR}/${_name})
  set(${_name}_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/${_name}-build)
endforeach(repo)

sbeClearJson(kg)

# Required order:
#  1. glslang
#  2. Vulkan-Headers
#  3. Vulkan-Loader
#  4. Vulkan-ValidationLayers
#  5. Vulkan-Tools
#  6. VulkanTools

file(STRINGS ${CMAKE_CURRENT_BINARY_DIR}/glslang/known_good.json known_good
  NEWLINE_CONSUME)
sbeParseJson(kg known_good)

foreach(commit ${kg.commits})
  string(REPLACE "-" "_" _name ${kg.commits_${commit}.name})
  set(_repo ${kg.commits_${commit}.subrepo})
  set(_url "https://github.com/${_repo}")
  fetch_sources(${_name} ${_url} ${kg.commits_${commit}.commit}
    SUBDIR ${kg.commits_${commit}.subdir}
    BASEDIR ${CMAKE_CURRENT_BINARY_DIR}/glslang
  )
  set(${_name}_SOURCE_DIR ${CMAKE_CURRENT_BINARY_DIR}/${_name})
  set(${_name}_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/${_name}-build)
endforeach(commit)

sbeClearJson(kg)

add_subdirectory(${glslang_SOURCE_DIR} ${glslang_BINARY_DIR})
add_subdirectory(${Vulkan_Headers_SOURCE_DIR} ${Vulkan_Headers_BINARY_DIR})

set(VulkanHeaders_FOUND TRUE)
set(VulkanHeaders_INCLUDE_DIR ${Vulkan_Headers_SOURCE_DIR}/include)
set(VulkanRegistry_FOUND TRUE)
set(VulkanRegistry_DIR ${Vulkan_Headers_SOURCE_DIR}/registry)

add_subdirectory(${Vulkan_Loader_SOURCE_DIR} ${Vulkan_Loader_BINARY_DIR})

set(GLSLANG_INSTALL_DIR ${glslang_SOURCE_DIR})
set(GLSLANG_SPIRV_INCLUDE_DIR ${glslang_SOURCE_DIR})
set(SPIRV_TOOLS_BINARY_ROOT ${spirv_tools_BINARY_DIR}/tools)
set(SPIRV_TOOLS_OPT_BINARY_ROOT ${spirv_TOOLS_BINARY_ROOT})
set(SPIRV_TOOLS_INCLUDE_DIR ${spirv_tools_SOURCE_DIR}/include)

add_subdirectory(${Vulkan_ValidationLayers_SOURCE_DIR}
  ${Vulkan_ValidationLayers_BINARY_DIR})

set(VULKAN_HEADERS_INSTALL_DIR ${VulkanHeaders_BINARY_DIR} CACHE PATH "")
set(VULKAN_LOADER_INSTALL_DIR ${VulkanLoader_BINARY_DIR} CACHE PATH "")
set(VULKAN_VALIDATIONLAYERS_INSTALL_DIR ${Vulkan_ValidationLayers_BINARY_DIR}
  CACHE PATH "")

add_subdirectory(${VulkanTools_SOURCE_DIR} ${VulkanTools_BINARY_DIR})
add_subdirectory(${Vulkan_Tools_SOURCE_DIR} ${Vulkan_Tools_BINARY_DIR})
