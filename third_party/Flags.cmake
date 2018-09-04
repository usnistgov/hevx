FetchContent_Declare(flags
  GIT_REPOSITORY https://github.com/sailormoon/flags
  GIT_SHALLOW TRUE # flags "should be" stable at HEAD
)

FetchContent_GetProperties(flags)
if(NOT flags_POPULATED)
  message(STATUS "Populating build dependency: flags")
  FetchContent_Populate(flags)
  add_library(flags INTERFACE)
  target_include_directories(flags INTERFACE ${flags_SOURCE_DIR}/include)
endif()