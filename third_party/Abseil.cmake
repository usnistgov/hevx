#set(ABSL_RUN_TESTS ${BUILD_DEPENDENCY_TESTING} CACHE BOOL "" FORCE)
set(ABSL_RUN_TESTS OFF CACHE BOOL "" FORCE)

message(STATUS "Populating abseil")
FetchContent_Populate(abseil
  GIT_REPOSITORY https://github.com/abseil/abseil-cpp
  GIT_SHALLOW TRUE # Abseil adheres to live-at-head
  SOURCE_DIR ${CMAKE_CURRENT_BINARY_DIR}/abseil
  UPDATE_DISCONNECTED ${THIRD_PARTY_UPDATE_DISCONNECTED}
  QUIET
)

add_subdirectory(${abseil_SOURCE_DIR} ${abseil_BINARY_DIR})
