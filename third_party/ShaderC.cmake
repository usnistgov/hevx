find_package(Git REQUIRED)
include(JSONParser)

if(NOT EXISTS ${CMAKE_CURRENT_BINARY_DIR}/shaderc_known_good.json)
  file(DOWNLOAD
    "https://raw.githubusercontent.com/google/shaderc/known-good/known_good.json"
    ${CMAKE_CURRENT_BINARY_DIR}/shaderc_known_good.json)
endif()

FetchContent_Declare(shaderc
  GIT_REPOSITORY https://github.com/google/shaderc
  GIT_SHALLOW TRUE
)

set(SHADERC_SKIP_TESTS ON CACHE BOOL "" FORCE)

function(fetch_source NAME COMMIT BASEDIR SUBDIR REPO)
  message(STATUS "Fetching ${NAME} source: ${COMMIT}")
  if(EXISTS ${BASEDIR}/${SUBDIR}/CMakeLists.txt)
    return()
  endif()

  execute_process(
    COMMAND ${GIT_EXECUTABLE} clone https://github.com/${REPO} ${SUBDIR}
    WORKING_DIRECTORY ${BASEDIR}
    RESULT_VARIABLE git_result)
  if(git_result)
    message(FATAL_ERROR "Unable to clone ${NAME} source: ${git_result}")
  endif()

  execute_process(
    COMMAND ${GIT_EXECUTABLE} checkout ${COMMIT}
    WORKING_DIRECTORY ${BASEDIR}/${SUBDIR}
    RESULT_VARIABLE git_result)
  if(git_result)
    message(FATAL_ERROR "Unable to checkout ${NAME} source: ${git_result}")
  endif()

  unset(git_result)
endfunction()

FetchContent_GetProperties(shaderc)
if(NOT shaderc_POPULATED)
  message(STATUS "Populating build dependency: shaderc")
  FetchContent_Populate(shaderc)

  file(STRINGS ${CMAKE_CURRENT_BINARY_DIR}/shaderc_known_good.json known_good NEWLINE_CONSUME)
  sbeParseJson(kg known_good)

  foreach(commit ${kg.commits})
    if(${kg.commits_${commit}.name} STREQUAL "shaderc")
      set(shaderc_commit ${kg.commits_${commit}.commit})
    elseif(${kg.commits_${commit}.name} STREQUAL "glslang")
      set(glslang_commit ${kg.commits_${commit}.commit})
      set(glslang_subrepo ${kg.commits_${commit}.subrepo})
      set(glslang_subdir ${kg.commits_${commit}.subdir})
    elseif(${kg.commits_${commit}.name} STREQUAL "spirv-tools")
      set(spirvtools_commit ${kg.commits_${commit}.commit})
      set(spirvtools_subrepo ${kg.commits_${commit}.subrepo})
      set(spirvtools_subdir ${kg.commits_${commit}.subdir})
    elseif(${kg.commits_${commit}.name} STREQUAL "spirv-headers")
      set(spirvheaders_commit ${kg.commits_${commit}.commit})
      set(spirvheaders_subrepo ${kg.commits_${commit}.subrepo})
      set(spirvheaders_subdir ${kg.commits_${commit}.subdir})
    endif()
  endforeach()

  message(STATUS "Fetching shaderc source: ${shaderc_commit}")
  if(NOT EXISTS ${shaderc_SOURCE_DIR}/CMakeLists.txt)
    execute_process(
      COMMAND ${GIT_EXECUTABLE} checkout ${shaderc_commit}
      WORKING_DIRECTORY ${shaderc_SOURCE_DIR}
      RESULT_VARIABLE git_result)
    if(git_result)
      message(FATAL_ERROR "Unable to checkout shaderc source: ${git_result}")
    endif()
  endif()

  fetch_source("glslang" ${glslang_commit} ${shaderc_SOURCE_DIR}
    ${glslang_subdir} ${glslang_subrepo})
  fetch_source("spirv-tools" ${spirvtools_commit} ${shaderc_SOURCE_DIR}
    ${spirvtools_subdir} ${spirvtools_subrepo})
  fetch_source("spirv-headers" ${spirvheaders_commit} ${shaderc_SOURCE_DIR}
    ${spirvheaders_subdir} ${spirvheaders_subrepo})

  add_subdirectory(${shaderc_SOURCE_DIR} ${shaderc_BINARY_DIR})

  unset(git_result)
  unset(shaderc_commit)
  unset(glslang_commit)
  unset(glslang_subrepo)
  unset(glslang_subdir)
  unset(spirvtools_commit)
  unset(spirvtools_subrepo)
  unset(spirvtools_subdir)
  unset(spirvheaders_commit)
  unset(spirvheaders_subrepo)
  unset(spirvheaders_subdir)
  sbeClearJson(kg)
endif()
