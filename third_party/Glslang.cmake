set(url https://github.com/KhronosGroup/glslang)
set(commit 5432f0dd8f331f15182681664d7486681e8514e6) # sdk-1.1.101.0

if(NOT EXISTS ${CMAKE_CURRENT_BINARY_DIR}/glslang/CMakeLists.txt)
  if(EXISTS ${CMAKE_CURRENT_BINARY_DIR}/glslang)
    file(REMOVE ${CMAKE_CURRENT_BINARY_DIR}/glslang)
  endif()

  execute_process(
    COMMAND ${GIT_EXECUTABLE} clone ${url} glslang
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    RESULT_VARIABLE res OUTPUT_VARIABLE out ERROR_VARIABLE out
  )
  if(res)
    message(FATAL_ERROR "Unable to clone glslang source: ${out}")
  endif()

  execute_process(
    COMMAND ${GIT_EXECUTABLE} reset --hard ${commit}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/glslang
    RESULT_VARIABLE res OUTPUT_VARIABLE out ERROR_VARIABLE out
  )
  if(res)
    message(FATAL_ERROR "Unable to checkout glslang source: ${out}")
  endif()

  execute_process(
    COMMAND ${Python3_EXECUTABLE} update_glslang_sources.py
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/glslang
    RESULT_VARIABLE res OUTPUT_VARIABLE out ERROR_VARIABLE out
  )
  if(res)
    message(FATAL_ERROR "Unable to update glslang sources: ${out}")
  endif()
endif()

set(BUILD_TESTING OFF)

message(STATUS "Populating glslang")
add_subdirectory(${CMAKE_CURRENT_BINARY_DIR}/glslang
  ${CMAKE_CURRENT_BINARY_DIR}/glslang-build
)