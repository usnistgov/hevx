find_package(Git REQUIRED)
include(JSONParser)

if(NOT EXISTS ${CMAKE_CURRENT_BINARY_DIR}/glslang_known_good.json)
  file(DOWNLOAD
    "https://raw.githubusercontent.com/KhronosGroup/glslang/master/known_good.json"
    ${CMAKE_CURRENT_BINARY_DIR}/glslang_known_good.json)
endif()

FetchContent_Declare(glslang
  GIT_REPOSITORY https://github.com/KhronosGroup/glslang
)

set(SHADERC_SKIP_TESTS ON CACHE BOOL "" FORCE)

function(fetch_source NAME COMMIT BASEDIR SUBDIR REPO)
  if(EXISTS ${BASEDIR}/${SUBDIR}/CMakeLists.txt)
    return()
  endif()
  message(STATUS "Fetching ${NAME} source: ${COMMIT}")

  execute_process(
    COMMAND ${GIT_EXECUTABLE} clone https://github.com/${REPO} ${SUBDIR}
    WORKING_DIRECTORY ${BASEDIR}
    RESULT_VARIABLE git_result OUTPUT_VARIABLE git_output ERROR_VARIABLE git_output)
  if(git_result)
    message(FATAL_ERROR "Unable to clone ${NAME} source: ${git_result}")
  endif()

  execute_process(
    COMMAND ${GIT_EXECUTABLE} reset --hard ${COMMIT}
    WORKING_DIRECTORY ${BASEDIR}/${SUBDIR}
    RESULT_VARIABLE git_result OUTPUT_VARIABLE git_output ERROR_VARIABLE git_output)
  if(git_result)
    message(FATAL_ERROR "Unable to checkout ${NAME} source: ${git_result}")
  endif()

  unset(git_result)
  unset(git_output)
endfunction()

FetchContent_GetProperties(glslang)
if(NOT glslang_POPULATED)
  message(STATUS "Populating build dependency: glslang")
  FetchContent_Populate(glslang)

  file(STRINGS ${CMAKE_CURRENT_BINARY_DIR}/glslang_known_good.json known_good NEWLINE_CONSUME)
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

  add_subdirectory(${glslang_SOURCE_DIR} ${glslang_BINARY_DIR})

  unset(git_result)
  unset(git_output)
  unset(spirvtools_commit)
  unset(spirvtools_subrepo)
  unset(spirvtools_subdir)
  unset(spirvheaders_commit)
  unset(spirvheaders_subrepo)
  unset(spirvheaders_subdir)
  sbeClearJson(kg)
endif()

