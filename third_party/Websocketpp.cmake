set(_websocketpp_git_tag 0.8.1)

set(BUILD_TESTS OFF CACHE BOOL "" FORCE)

message(STATUS "Populating websocketpp")
FetchContent_Populate(websocketpp
  GIT_REPOSITORY https://github.com/zaphoyd/websocketpp
  GIT_SHALLOW TRUE GIT_TAG ${_websocketpp_git_tag}
  SOURCE_DIR ${CMAKE_CURRENT_BINARY_DIR}/websocketpp
  QUIET
)

add_subdirectory(${websocketpp_SOURCE_DIR} ${websocketpp_BINARY_DIR})
