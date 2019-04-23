set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

message(STATUS "Populating googletest")
FetchContent_Populate(googletest
  GIT_REPOSITORY https://github.com/google/googletest
  GIT_SHALLOW TRUE # GoogleTest adheres to live-at-head
  SOURCE_DIR ${CMAKE_CURRENT_BINARY_DIR}/googletest
  UPDATE_DISCONNECTED ${THIRD_PARTY_UPDATE_DISCONNECTED}
  QUIET
)

add_subdirectory(${googletest_SOURCE_DIR} ${googletest_BINARY_DIR})
