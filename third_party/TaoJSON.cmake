set(TAOCPP_JSON_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(TAOCPP_JSON_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

FetchContent_Declare(taojson
  GIT_REPOSITORY https://github.com/taocpp/json
  GIT_SHALLOW TRUE # TaoJSON HEAD "should be" stable
)

FetchContent_GetProperties(taojson)
if(NOT taojson_POPULATED)
  message(STATUS "Populating build dependency: taojson")
  FetchContent_Populate(taojson)
  add_library(json INTERFACE)
  target_include_directories(json INTERFACE ${taojson_SOURCE_DIR}/include)
endif()
