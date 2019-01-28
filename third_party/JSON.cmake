set(JSON_BuildTests ${BUILD_DEPENDENCY_TESTING} CACHE BOOL "" FORCE)

message(STATUS "Populating build dependency: json")
FetchContent_Populate(json
  GIT_REPOSITORY https://github.com/nlohmann/json
  GIT_SHALLOW TRUE # json HEAD "should be" stable
  QUIET
)

add_subdirectory(${json_SOURCE_DIR} ${json_BINARY_DIR})
