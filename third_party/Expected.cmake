set(EXPECTED_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(EXPECTED_ENABLE_DOCS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(expected
  GIT_REPOSITORY https://github.com/TartanLlama/expected
  GIT_SHALLOW TRUE # Expected "should be" stable at HEAD
)

FetchContent_GetProperties(expected)
if(NOT expected_POPULATED)
  message(STATUS "Populating build dependency: expected")
  FetchContent_Populate(expected)
  add_library(expected INTERFACE)
  target_include_directories(expected INTERFACE ${expected_SOURCE_DIR})
endif()
