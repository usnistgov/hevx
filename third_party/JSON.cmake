set(JSON_BuildTests ${BUILD_DEPENDENCY_TESTING} CACHE BOOL "" FORCE)

message(STATUS "Populating json")
FetchContent_Populate(json
  GIT_REPOSITORY https://github.com/nlohmann/json
  GIT_SHALLOW TRUE # json HEAD "should be" stable
  SOURCE_DIR ${CMAKE_CURRENT_BINARY_DIR}/json
  UPDATE_DISCONNECTED ${THIRD_PARTY_UPDATE_DISCONNECTED}
  QUIET
)

add_subdirectory(${json_SOURCE_DIR} ${json_BINARY_DIR})
