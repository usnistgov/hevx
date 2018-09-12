set(_protobuf_git_tag v3.6.1)

set(protobuf_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(protobuf_WITH_ZLIB OFF CACHE BOOL "" FORCE)
set(protobuf_MSVC_STATIC_RUNTIME OFF CACHE BOOL "" FORCE)

FetchContent_Declare(protobuf
  GIT_REPOSITORY https://github.com/protocolbuffers/protobuf
  GIT_SHALLOW TRUE GIT_TAG ${_protobuf_git_tag}
  SOURCE_SUBDIR cmake
)

FetchContent_GetProperties(protobuf)
if(NOT protobuf_POPULATED)
  message(STATUS "Populating build dependency: protobuf")
  FetchContent_Populate(protobuf)
  add_subdirectory(${protobuf_SOURCE_DIR}/cmake ${protobuf_BINARY_DIR})
endif()

unset(_protobuf_git_tag)
