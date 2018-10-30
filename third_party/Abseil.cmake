FetchContent_Declare(abseil
  GIT_REPOSITORY https://github.com/abseil/abseil-cpp
  GIT_SHALLOW TRUE # Abseil adheres to live-at-head
  PATCH_COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/AbseilPatch.cmake
)

FetchContent_GetProperties(abseil)
if(NOT abseil_POPULATED)
  message(STATUS "Populating build dependency: abseil")
  FetchContent_Populate(abseil)
  add_subdirectory(${abseil_SOURCE_DIR} ${abseil_BINARY_DIR})
endif()
