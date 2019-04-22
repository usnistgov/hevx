set(url https://github.com/KhronosGroup/glslang)
set(commit e06c7e9a515b716c731bda13f507546f107775d1) # sdk-1.1.106.0

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