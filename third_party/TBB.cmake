set(_tbb_git_tag 2018_U5)

FetchContent_Declare(tbb
  GIT_REPOSITORY https://github.com/01org/tbb
  GIT_SHALLOW TRUE GIT_TAG ${_tbb_git_tag}
)

FetchContent_GetProperties(tbb)
if(NOT tbb_POPULATED)
  message(STATUS "Populating build dependency: tbb")
  FetchContent_Populate(tbb)
  include(${tbb_SOURCE_DIR}/cmake/TBBBuild.cmake)

  tbb_build(TBB_ROOT ${tbb_SOURCE_DIR} CONFIG_DIR TBB_DIR
    MAKE_ARGS tbb_build_prefix= tbb_build_dir=${tbb_BINARY_DIR})
  set(TBB_DIR ${TBB_DIR} PARENT_SCOPE)
endif()

unset(_tbb_git_tag)
