set(_fmtlib_git_tag 5.2.1)

FetchContent_Declare(fmtlib
  GIT_REPOSITORY https://github.com/fmtlib/fmt
  GIT_SHALLOW TRUE GIT_TAG ${_fmtlib_git_tag}
)

FetchContent_GetProperties(fmtlib)
if(NOT fmtlib_POPULATED)
  message(STATUS "Populating build dependency: fmtlib")
  FetchContent_Populate(fmtlib)
  add_subdirectory(${fmtlib_SOURCE_DIR} ${fmtlib_BINARY_DIR})
endif()

unset(_fmtlib_git_tag)
