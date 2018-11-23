set(_glm_git_tag HEAD)

set(GLM_TEST_ENABLE ${BUILD_DEPENDENCY_TESTING} CACHE BOOL "" FORCE)

FetchContent_Declare(glm
  GIT_REPOSITORY https://github.com/g-truc/glm
  GIT_SHALLOW TRUE GIT_TAG ${_glm_git_tag}
)

FetchContent_GetProperties(glm)
if(NOT glm_POPULATED)
  message(STATUS "Populating build dependency: glm")
  FetchContent_Populate(glm)
  add_subdirectory(${glm_SOURCE_DIR} ${glm_BINARY_DIR})
endif()

unset(_glm_git_tag)
