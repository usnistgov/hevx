FetchContent_Declare(gsl
  GIT_REPOSITORY https://github.com/Microsoft/GSL
  GIT_SHALLOW TRUE # GSL *should* be stable at HEAD
)

FetchContent_GetProperties(gsl)
if(NOT gsl_POPULATED)
  message(STATUS "Populating build dependency: gsl")
  FetchContent_Populate(gsl)
  add_library(gsl INTERFACE)
  target_include_directories(gsl INTERFACE ${gsl_SOURCE_DIR}/include)
endif()
