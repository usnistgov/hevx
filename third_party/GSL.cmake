set(GSL_TEST ${BUILD_DEPENDENCY_TESTING} CACHE BOOL "" FORCE)

FetchContent_Declare(gsl
  GIT_REPOSITORY https://github.com/Microsoft/GSL
  GIT_SHALLOW TRUE # GSL *should* be stable at HEAD
)

FetchContent_GetProperties(gsl)
if(NOT gsl_POPULATED)
  message(STATUS "Populating build dependency: gsl")
  FetchContent_Populate(gsl)
  add_subdirectory(${gsl_SOURCE_DIR} ${gsl_BINARY_DIR})
endif()
