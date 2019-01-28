find_package(Git REQUIRED)
include(FetchContent)
include(JSONParser)

set(Python_ADDITIONAL_VERSIONS 3.7 3.6 3.5 3.4)
find_package(PythonInterp 3 REQUIRED)

# used by some of the scripts below
function(fetch_sources NAME URL COMMIT)
  set(one_value_args BASEDIR SUBDIR)
  cmake_parse_arguments(PARSE_ARGV ${ARGC} FS "" "${one_value_args}" "")

  if(NOT FS_BASEDIR)
    set(BASEDIR ${CMAKE_CURRENT_BINARY_DIR})
  else()
    set(BASEDIR ${FS_BASEDIR})
  endif()

  if(NOT FS_SUBDIR)
    set(SUBDIR ${NAME})
  else()
    set(SUBDIR ${FS_SUBDIR})
  endif()

  if(EXISTS ${BASEDIR}/${SUBDIR}/CMakeLists.txt)
    return()
  endif()
  message(STATUS "Fetching ${NAME}:${COMMIT} to ${SUBDIR}")

  execute_process(
    COMMAND ${GIT_EXECUTABLE} clone ${URL} ${SUBDIR}
    WORKING_DIRECTORY ${BASEDIR}
    RESULT_VARIABLE git_res OUTPUT_VARIABLE git_out ERROR_VARIABLE git_out)
  if(git_res)
    message(FATAL_ERROR "Unable to clone ${NAME} source: ${git_out}")
  endif()

  execute_process(
    COMMAND ${GIT_EXECUTABLE} reset --hard ${COMMIT}
    WORKING_DIRECTORY ${BASEDIR}/${SUBDIR}
    RESULT_VARIABLE git_res OUTPUT_VARIABLE git_out ERROR_VARIABLE git_out)
  if(git_res)
    message(FATAL_ERROR "Unable to checkout ${NAME} source: ${git_out}")
  endif()
endfunction(fetch_sources)

include(GoogleTest.cmake)
include(Abseil.cmake)
include(Expected.cmake)
include(Fmtlib.cmake)
include(GSL.cmake)
include(GLM.cmake)
include(JSON.cmake)
include(Spdlog.cmake)
include(STB.cmake)
include(TBB.cmake)
include(NNG.cmake)
include(Protobuf.cmake)
include(VulkanSDK.cmake)
#include(VulkanValidationLayers.cmake)
#include(VulkanHeaders.cmake)
#include(VulkanLoader.cmake)
#include(Glslang.cmake)
include(VulkanMemoryAllocator.cmake)
include(FlextGL.cmake)
include(DearImGui.cmake)
include(Flags.cmake)
