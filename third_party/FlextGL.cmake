message(STATUS "Populating flextgl")
FetchContent_Populate(flextgl
  GIT_REPOSITORY https://github.com/mosra/flextgl
  GIT_SHALLOW TRUE # flextGL "should be" stable at HEAD
  SOURCE_DIR ${CMAKE_CURRENT_BINARY_DIR}/flextgl
  UPDATE_DISCONNECTED ${THIRD_PARTY_UPDATE_DISCONNECTED}
  QUIET
)

set(FLEXTGL_SOURCE_DIR ${flextgl_SOURCE_DIR} CACHE STRING "" FORCE)
