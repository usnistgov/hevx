set(EXPECTED_ENABLE_DOCS OFF CACHE BOOL "" FORCE)
set(EXPECTED_ENABLE_TESTS ${BUILD_DEPENDENCY_TESTING} CACHE BOOL "" FORCE)

message(STATUS "Populating expected")
FetchContent_Populate(expected
  GIT_REPOSITORY https://github.com/TartanLlama/expected
  GIT_SHALLOW TRUE # Expected "should be" stable at HEAD
  PATCH_COMMAND
    ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/ExpectedPatch.cmake
  SOURCE_DIR ${CMAKE_CURRENT_BINARY_DIR}/expected
  UPDATE_DISCONNECTED ${THIRD_PARTY_UPDATE_DISCONNECTED}
  QUIET
)

add_subdirectory(${expected_SOURCE_DIR} ${expected_BINARY_DIR})
