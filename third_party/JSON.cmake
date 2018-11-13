set(JSON_BuildTests OFF CACHE INTERNAL "")

FetchContent_Declare(json
  GIT_REPOSITORY https://github.com/nlohmann/json
  GIT_SHALLOW TRUE # json HEAD "should be" stable
)

FetchContent_GetProperties(json)
if(NOT json_POPULATED)
  message(STATUS "Populating build dependency: json")
  FetchContent_Populate(json)
  add_subdirectory(${json_SOURCE_DIR} ${json_BINARY_DIR})
endif()
