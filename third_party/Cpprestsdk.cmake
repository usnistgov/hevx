if(NOT WIN32)

set(BUILD_TESTS OFF)
set(BUILD_SAMPLES OFF)
set(BUILD_EXAMPLES OFF)

message(STATUS "Populating cpprestsdk")
FetchContent_Populate(cpprestsdk
  GIT_REPOSITORY https://github.com/Microsoft/cpprestsdk
  GIT_SHALLOW TRUE GIT_TAG v2.10.10
  UPDATE_DISCONNECTED ${THIRD_PARTY_UPDATE_DISCONNECTED}
  SOURCE_DIR ${CMAKE_CURRENT_BINARY_DIR}/cpprestsdk
  QUIET
)

add_subdirectory(${cpprestsdk_SOURCE_DIR} ${cpprestsdk_BINARY_DIR})
add_library(cpprestsdk::cpprest ALIAS cpprest)

endif()
