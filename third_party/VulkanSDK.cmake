set(_git_tag sdk-1.1.97)
set(_base_url "https://raw.githubusercontent.com/KhronosGroup")

if(NOT EXISTS ${CMAKE_CURRENT_BINARY_DIR}/known_good.json)
  file(DOWNLOAD
    ${_base_url}/Vulkan-ValidationLayers/${_git_tag}/scripts/known_good.json
    ${CMAKE_CURRENT_BINARY_DIR}/known_good.json)
endif()

file(STRINGS ${CMAKE_CURRENT_BINARY_DIR}/known_good.json
known_good NEWLINE_CONSUME)
sbeParseJson(kg known_good)

foreach(repo ${kg.repos})
  fetch_sources(${kg.repos_${repo}.name} ${kg.repos_${repo}.url}
    ${kg.repos_${repo}.commit})
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
  set(_repo ${kg.commits_${commit}.subrepo})
  set(_url "https://github.com/${_repo}")
  fetch_sources(${kg.commits_${commit}.name} ${_url}
    ${kg.commits_${commit}.commit}
    SUBDIR ${kg.commits_${commit}.subdir}
    BASEDIR ${CMAKE_CURRENT_BINARY_DIR}/glslang
  )
endforeach(commit)

sbeClearJson(kg)

