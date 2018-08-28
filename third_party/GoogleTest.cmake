set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

FetchContent_Declare(googletest
  GIT_REPOSITORY https://github.com/google/googletest
  GIT_SHALLOW TRUE # GoogleTest adheres to live-at-head
)

FetchContent_GetProperties(googletest)
if(NOT googletest_POPULATED)
  message(STATUS "Populating build dependency: googletest")
  FetchContent_Populate(googletest)
  add_subdirectory(${googletest_SOURCE_DIR} ${googletest_BINARY_DIR})
endif()
