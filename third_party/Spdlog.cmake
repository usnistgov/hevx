set(_spdlog_git_tag v1.1.0)

FetchContent_Declare(spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog
  GIT_SHALLOW TRUE GIT_TAG ${_spdlog_git_tag}
)

FetchContent_GetProperties(spdlog)
if(NOT spdlog_POPULATED)
  message(STATUS "Populating build dependency: spdlog")
  FetchContent_Populate(spdlog)
  file(REMOVE ${spdlog_SOURCE_DIR}/include/spdlog/tweakme.h)
  file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/tweakme.h
       DESTINATION ${spdlog_SOURCE_DIR}/include/spdlog)

  add_library(spdlog INTERFACE)
  target_include_directories(spdlog INTERFACE ${spdlog_SOURCE_DIR}/include
    $<TARGET_PROPERTY:fmt,INCLUDE_DIRECTORIES>
  )
endif()

unset(_spdlog_git_tag)
