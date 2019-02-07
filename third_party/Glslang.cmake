set(url https://github.com/KhronosGroup/glslang)
set(commit 2898223375d57fb3974f24e1e944bb624f67cb73) # sdk-1.1.97.0

if(NOT EXISTS ${CMAKE_CURRENT_BINARY_DIR}/glslang/CMakeLists.txt)
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
    COMMAND ${PYTHON_EXECUTABLE} update_glslang_sources.py
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