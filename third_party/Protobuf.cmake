set(_protobuf_git_tag v3.6.1)

set(protobuf_WITH_ZLIB OFF CACHE BOOL "" FORCE)
set(protobuf_MSVC_STATIC_RUNTIME OFF CACHE BOOL "" FORCE)
#set(protobuf_BUILD_TESTS ${BUILD_DEPENDENCY_TESTING} CACHE BOOL "" FORCE)
set(protobuf_BUILD_TESTS OFF CACHE BOOL "" FORCE)

message(STATUS "Populating protobuf")
FetchContent_Populate(protobuf
  GIT_REPOSITORY https://github.com/protocolbuffers/protobuf
  GIT_SHALLOW TRUE GIT_TAG ${_protobuf_git_tag}
  UPDATE_DISCONNECTED ${THIRD_PARTY_UPDATE_DISCONNECTED}
  SOURCE_DIR ${CMAKE_CURRENT_BINARY_DIR}/protobuf
  SOURCE_SUBDIR cmake
  QUIET
)

add_subdirectory(${protobuf_SOURCE_DIR}/cmake ${protobuf_BINARY_DIR})
set(PROTOBUF_INCLUDE_DIR ${protobuf_SOURCE_DIR}/src CACHE PATH "" FORCE)
