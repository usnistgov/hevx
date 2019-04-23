message(STATUS "Populating flags")
FetchContent_Populate(flags
  GIT_REPOSITORY https://github.com/sailormoon/flags
  GIT_SHALLOW TRUE # flags "should be" stable at HEAD
  SOURCE_DIR ${CMAKE_CURRENT_BINARY_DIR}/flags
  UPDATE_DISCONNECTED ${THIRD_PARTY_UPDATE_DISCONNECTED}
  QUIET
)

add_library(flags INTERFACE)
target_include_directories(flags INTERFACE ${flags_SOURCE_DIR}/include)
