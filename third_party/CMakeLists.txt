find_package(Git REQUIRED)
include(FetchContent)
include(JSONParser)

function(fetch_sources NAME URL COMMIT)
  set(options NO_RECURSE_SUBMODULES)
  set(one_value_args SUBDIR BASEDIR)
  cmake_parse_arguments(PARSE_ARGV 3 FS "${options}" "${one_value_args}" "")

  if(FS_NO_RECURSE_SUBMODULES)
    set(RECURSE_SUBMODULES "")
  else()
    set(RECURSE_SUBMODULES "--recurse-submodules")
  endif()

  if(NOT FS_SUBDIR)
    set(SUBDIR ${NAME})
  else()
    set(SUBDIR ${FS_SUBDIR})
  endif()

  if(NOT FS_BASEDIR)
    set(BASEDIR ${CMAKE_CURRENT_BINARY_DIR})
  else()
    set(BASEDIR ${FS_BASEDIR})
  endif()

  if(EXISTS ${BASEDIR}/${SUBDIR}/CMakeLists.txt)
    return()
  endif()

  execute_process(
    COMMAND ${GIT_EXECUTABLE} clone ${RECURSE_SUBMODULES} ${URL} ${SUBDIR}
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

# The following packages must be included first and in this order
include(GoogleTest.cmake)
include(GLM.cmake)

# These packages can be included in any order
include(Abseil.cmake)
include(Expected.cmake)
include(Flags.cmake)
include(FlextGL.cmake)
include(Fmtlib.cmake)
include(GSL.cmake)
include(ImGui.cmake)
include(JSON.cmake)
include(NNG.cmake)
include(Protobuf.cmake)
include(Spdlog.cmake)
include(STB.cmake)
include(TBB.cmake)
include(Vulkan.cmake)
include(VulkanMemoryAllocator.cmake)
