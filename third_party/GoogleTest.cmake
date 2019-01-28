set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

message(STATUS "Populating build dependency: googletest")
FetchContent_Populate(googletest
  GIT_REPOSITORY https://github.com/google/googletest
  GIT_SHALLOW TRUE # GoogleTest adheres to live-at-head
  QUIET
)

add_subdirectory(${googletest_SOURCE_DIR} ${googletest_BINARY_DIR})
