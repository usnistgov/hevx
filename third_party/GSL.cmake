#set(GSL_TEST ${BUILD_DEPENDENCY_TESTING} CACHE BOOL "" FORCE)
set(GSL_TEST OFF CACHE BOOL "" FORCE)

message(STATUS "Populating build dependency: gsl")
FetchContent_Populate(gsl
  GIT_REPOSITORY https://github.com/Microsoft/GSL
  GIT_SHALLOW TRUE # GSL *should* be stable at HEAD
  QUIET
)

add_subdirectory(${gsl_SOURCE_DIR} ${gsl_BINARY_DIR})
