if(NOT EXISTS ${CMAKE_CURRENT_BINARY_DIR}/glslang_known_good.json)
  file(DOWNLOAD
    "https://raw.githubusercontent.com/KhronosGroup/glslang/master/known_good.json"
    ${CMAKE_CURRENT_BINARY_DIR}/glslang_known_good.json)
endif()

message(STATUS "Populating build dependency: glslang")
FetchContent_Populate(glslang
  GIT_REPOSITORY https://github.com/KhronosGroup/glslang
  GIT_TAG HEAD
  QUIET
)

file(STRINGS ${CMAKE_CURRENT_BINARY_DIR}/glslang_known_good.json known_good
  NEWLINE_CONSUME)
sbeParseJson(kg known_good)

foreach(commit ${kg.commits})
  if(${kg.commits_${commit}.name} STREQUAL "spirv-tools")
    set(spirvtools_commit ${kg.commits_${commit}.commit})
    set(spirvtools_subrepo ${kg.commits_${commit}.subrepo})
    set(spirvtools_subdir ${kg.commits_${commit}.subdir})
  elseif(${kg.commits_${commit}.name} STREQUAL "spirv-tools/external/spirv-headers")
    set(spirvheaders_commit ${kg.commits_${commit}.commit})
    set(spirvheaders_subrepo ${kg.commits_${commit}.subrepo})
    set(spirvheaders_subdir ${kg.commits_${commit}.subdir})
  endif()
endforeach()

fetch_source("spirv-tools" ${spirvtools_commit} ${glslang_SOURCE_DIR}
  ${spirvtools_subdir} ${spirvtools_subrepo})
fetch_source("spirv-headers" ${spirvheaders_commit} ${glslang_SOURCE_DIR}
  ${spirvheaders_subdir} ${spirvheaders_subrepo})

sbeClearJson(kg)

add_subdirectory(${glslang_SOURCE_DIR} ${glslang_BINARY_DIR})
set(GLSLANG_SOURCE_DIR ${glslang_SOURCE_DIR} CACHE STRING "" FORCE)
set(GLSLANG_BINARY_DIR ${glslang_BINARY_DIR} CACHE STRING "" FORCE)
