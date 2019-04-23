set(_glm_git_tag HEAD)

set(GLM_TEST_ENABLE ${BUILD_DEPENDENCY_TESTING} CACHE BOOL "" FORCE)

message(STATUS "Populating glm")
FetchContent_Populate(glm
  GIT_REPOSITORY https://github.com/g-truc/glm
  GIT_SHALLOW TRUE GIT_TAG ${_glm_git_tag}
  SOURCE_DIR ${CMAKE_CURRENT_BINARY_DIR}/glm
  UPDATE_DISCONNECTED ${THIRD_PARTY_UPDATE_DISCONNECTED}
  QUIET
)

add_subdirectory(${glm_SOURCE_DIR} ${glm_BINARY_DIR})
