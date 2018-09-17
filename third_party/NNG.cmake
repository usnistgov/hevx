set(_nng_git_tag v1.0.1)

set(NNG_TESTS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(nng
  GIT_REPOSITORY https://github.com/nanomsg/nng
  GIT_SHALLOW TRUE GIT_TAG ${_nng_git_tag}
)

FetchContent_GetProperties(nng)
if(NOT nng_POPULATED)
  message(STATUS "Populating build dependency: nng")
  FetchContent_Populate(nng)
  add_subdirectory(${nng_SOURCE_DIR} ${nng_BINARY_DIR})
  set(NNG_INCLUDE_DIR ${nng_SOURCE_DIR}/src CACHE PATH "")
endif()

unset(_nng_git_tag)
