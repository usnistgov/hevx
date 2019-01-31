set(_git_tag sdk-1.1.97.0)

fetch_sources(Vulkan_ValidationLayers
  https://github.com/KhronosGroup/Vulkan-ValidationLayers ${_git_tag})
set(Vulkan_ValidationLayers_SOURCE_DIR
  ${CMAKE_CURRENT_BINARY_DIR}/Vulkan_ValidationLayers)
set(Vulkan_ValidationLayers_BINARY_DIR
  ${CMAKE_CURRENT_BINARY_DIR}/Vulkan_ValidationLayers-build)

file(STRINGS ${Vulkan_ValidationLayers_SOURCE_DIR}/scripts/known_good.json
  known_good NEWLINE_CONSUME)
sbeParseJson(kg known_good)

foreach(repo ${kg.repos})
  string(REPLACE "-" "_" _name ${kg.repos_${repo}.name})
  set(${_name}_URL ${kg.repos_${repo}.url})
  set(${_name}_COMMIT ${kg.repos_${repo}.commit})
  set(${_name}_SOURCE_DIR ${CMAKE_CURRENT_BINARY_DIR}/${_name})
  set(${_name}_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/${_name}-build)
endforeach(repo)

sbeClearJson(kg)

# Required order:
#  1. glslang
#  2. Vulkan-Headers
#  3. Vulkan-Loader
#  4. Vulkan-ValidationLayers

message(STATUS "Populating glslang")
fetch_sources(glslang ${glslang_URL} ${glslang_COMMIT})

file(STRINGS ${CMAKE_CURRENT_BINARY_DIR}/glslang/known_good.json known_good
  NEWLINE_CONSUME)
sbeParseJson(kg known_good)

foreach(commit ${kg.commits})
  if(${kg.commits_${commit}.name} STREQUAL "spirv-tools")
    set(_name "spirv_tools")
  elseif(${kg.commits_${commit}.name} STREQUAL "spirv-tools/external/spirv-headers")
    set(_name "spirv_headers")
  endif()

  set(${_name}_URL https://github.com/${kg.commits_${commit}.subrepo})
  set(${_name}_COMMIT ${kg.commits_${commit}.commit})
  set(${_name}_SUBDIR ${kg.commits_${commit}.subdir})
  set(${_name}_SOURCE_DIR ${CMAKE_CURRENT_BINARY_DIR}/${_name})
  set(${_name}_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/${_name}-build)
endforeach(commit)

sbeClearJson(kg)

message(STATUS "Fetching SPIRV-tools")
fetch_sources(spirv_tools ${spirv_tools_URL} ${spirv_tools_COMMIT}
  SUBDIR ${spirv_tools_SUBDIR} BASEDIR ${glslang_SOURCE_DIR}
)

message(STATUS "Fetching SPIRV-Headers")
fetch_sources(spirv_headers ${spirv_headers_URL} ${spirv_headers_COMMIT}
  SUBDIR ${spirv_headers_SUBDIR} BASEDIR ${glslang_SOURCE_DIR}
)

set(BUILD_TESTING OFF)
add_subdirectory(${glslang_SOURCE_DIR} ${glslang_BINARY_DIR})

message(STATUS "Populating Vulkan-Headers")
fetch_sources(Vulkan_Headers ${Vulkan_Headers_URL} ${Vulkan_Headers_COMMIT})
add_subdirectory(${Vulkan_Headers_SOURCE_DIR} ${Vulkan_Headers_BINARY_DIR})

set(VulkanHeaders_FOUND TRUE)
set(VulkanHeaders_INCLUDE_DIR ${Vulkan_Headers_SOURCE_DIR}/include CACHE STRING "")
set(VulkanRegistry_FOUND TRUE)
set(VulkanRegistry_DIR ${Vulkan_Headers_SOURCE_DIR}/registry)

message(STATUS "Populating Vulkan-Loader")
fetch_sources(Vulkan_Loader ${Vulkan_Loader_URL} ${Vulkan_Loader_COMMIT})
add_subdirectory(${Vulkan_Loader_SOURCE_DIR} ${Vulkan_Loader_BINARY_DIR})

set(GLSLANG_INSTALL_DIR ${glslang_SOURCE_DIR})
set(GLSLANG_SPIRV_INCLUDE_DIR ${glslang_SOURCE_DIR})
set(SPIRV_TOOLS_BINARY_ROOT ${spirv_tools_BINARY_DIR}/tools)
set(SPIRV_TOOLS_OPT_BINARY_ROOT ${spirv_TOOLS_BINARY_ROOT})
set(SPIRV_TOOLS_INCLUDE_DIR ${spirv_tools_SOURCE_DIR}/include)

set(BUILD_WSI_WAYLAND_SUPPORT OFF CACHE BOOL "")

message(STATUS "Populating Vulkan-ValidationLayers")
add_subdirectory(${Vulkan_ValidationLayers_SOURCE_DIR}
  ${Vulkan_ValidationLayers_BINARY_DIR})
