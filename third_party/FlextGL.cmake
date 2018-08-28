FetchContent_Declare(flextgl
  GIT_REPOSITORY https://github.com/mosra/flextgl
  GIT_SHALLOW TRUE # flextGL "should be" stable at HEAD
)

FetchContent_GetProperties(flextgl)
if(NOT flextgl_POPULATED)
  message(STATUS "Populating build dependency: flextgl")
  FetchContent_Populate(flextgl)
endif()

