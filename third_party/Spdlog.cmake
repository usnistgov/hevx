set(_spdlog_git_tag v1.1.0)

#set(SPDLOG_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
#set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "" FORCE)
#set(SPDLOG_BUILD_TESTING ${BUILD_DEPENDENCY_TESTING} CACHE BOOL "" FORCE)

message(STATUS "Populating build dependency: spdlog")
FetchContent_Populate(spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog
  GIT_SHALLOW TRUE GIT_TAG ${_spdlog_git_tag}
  QUIET
)

file(REMOVE ${spdlog_SOURCE_DIR}/include/spdlog/tweakme.h)
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/tweakme.h
      DESTINATION ${spdlog_SOURCE_DIR}/include/spdlog)
add_library(spdlog INTERFACE)
    target_include_directories(spdlog INTERFACE ${spdlog_SOURCE_DIR}/include
    $<TARGET_PROPERTY:fmt,INCLUDE_DIRECTORIES>
)
