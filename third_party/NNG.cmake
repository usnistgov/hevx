set(_nng_git_tag v1.0.1)

set(NNG_TESTS ${BUILD_DEPENDENCY_TESTING} CACHE BOOL "" FORCE)

message(STATUS "Populating nng")
FetchContent_Populate(nng
  GIT_REPOSITORY https://github.com/nanomsg/nng
  GIT_SHALLOW TRUE GIT_TAG ${_nng_git_tag}
  UPDATE_DISCONNECTED ${THIRD_PARTY_UPDATE_DISCONNECTED}
  SOURCE_DIR ${CMAKE_CURRENT_BINARY_DIR}/nng
  QUIET
)

add_subdirectory(${nng_SOURCE_DIR} ${nng_BINARY_DIR})
set(NNG_INCLUDE_DIR ${nng_SOURCE_DIR}/src CACHE PATH "" FORCE)
