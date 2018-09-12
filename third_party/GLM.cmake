set(_glm_git_tag HEAD)

set(GLM_TEST_ENABLE OFF CACHE BOOL "" FORCE)

FetchContent_Declare(glm
  GIT_REPOSITORY https://github.com/g-truc/glm
  GIT_SHALLOW TRUE GIT_TAG ${_glm_git_tag}
)

FetchContent_GetProperties(glm)
if(NOT glm_POPULATED)
  message(STATUS "Populating build dependency: glm")
  FetchContent_Populate(glm)
  add_library(glm INTERFACE)
  target_include_directories(glm INTERFACE ${glm_SOURCE_DIR})
  target_compile_definitions(glm
    INTERFACE GLM_FORCE_RADIANS GLM_FORCE_DEPTH_ZERO_TO_ONE)
endif()

unset(_glm_git_tag)
