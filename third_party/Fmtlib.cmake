set(_fmtlib_git_tag 5.2.1)

# fmtlib testing locally includes gmock/gtest which conflicts
set(FMT_TEST OFF CACHE BOOL "" FORCE)

message(STATUS "Populating build dependency: fmtlib")
  FetchContent_Populate(fmtlib
  GIT_REPOSITORY https://github.com/fmtlib/fmt
  GIT_SHALLOW TRUE GIT_TAG ${_fmtlib_git_tag}
  QUIET
)

add_subdirectory(${fmtlib_SOURCE_DIR} ${fmtlib_BINARY_DIR})
