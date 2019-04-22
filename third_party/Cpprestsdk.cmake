set(_cpprestsdk_git_tag v2.10.10)

set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

message(STATUS "Populating cpprestsdk")
FetchContent_Populate(cpprestsdk
  GIT_REPOSITORY https://github.com/Microsoft/cpprestsdk
  GIT_SHALLOW TRUE GIT_TAG ${_cpprestsdk_git_tag}
  SOURCE_DIR ${CMAKE_CURRENT_BINARY_DIR}/cpprestsdk
  QUIET
)

add_subdirectory(${cpprestsdk_SOURCE_DIR} ${cpprestsdk_BINARY_DIR})
add_library(cpprestsdk::cpprest ALIAS cpprest)
