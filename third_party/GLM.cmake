set(_glm_git_tag HEAD)

set(GLM_TEST_ENABLE ${BUILD_DEPENDENCY_TESTING} CACHE BOOL "" FORCE)

message(STATUS "Populating build dependency: glm")
FetchContent_Populate(glm
  GIT_REPOSITORY https://github.com/g-truc/glm
  GIT_SHALLOW TRUE GIT_TAG ${_glm_git_tag}
  QUIET
)

add_subdirectory(${glm_SOURCE_DIR} ${glm_BINARY_DIR})
