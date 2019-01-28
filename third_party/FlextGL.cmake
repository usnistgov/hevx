message(STATUS "Populating build dependency: flextgl")
FetchContent_Populate(flextgl
  GIT_REPOSITORY https://github.com/mosra/flextgl
  GIT_SHALLOW TRUE # flextGL "should be" stable at HEAD
  QUIET
)

set(FLEXTGL_SOURCE_DIR ${flextgl_SOURCE_DIR} CACHE STRING "" FORCE)
