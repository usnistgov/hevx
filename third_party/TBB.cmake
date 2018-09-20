#if(WIN32)

  set(_tbb_prefix "tbb2018_20180618oss")

  if(WIN32)
    set(_tbb_fn "${_tbb_prefix}_win.zip")
  elseif(UNIX)
    set(_tbb_fn "${_tbb_prefix}_lin.tgz")
  endif()

  set(_tbb_rel "2018_U5")
  set(_tbb_url "https://github.com/01org/tbb/releases/download/${_tbb_rel}")

  if(NOT EXISTS "${CMAKE_CURRENT_BINARY_DIR}/${_tbb_fn}")
    message(STATUS "Populating build dependency: tbb")
    file(DOWNLOAD "${_tbb_url}/${_tbb_fn}" ${CMAKE_CURRENT_BINARY_DIR}/${_tbb_fn})
    execute_process(COMMAND ${CMAKE_COMMAND} -E tar xf ${_tbb_fn}
      WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
  endif()

  list(APPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_BINARY_DIR}/${_tbb_prefix}")
  find_package(TBB REQUIRED tbb)

  if(WIN32)
    get_filename_component(_tbb_lib_dir ${TBB_DIR}/../lib/intel64/vc14 ABSOLUTE)
    get_filename_component(_tbb_bin_dir ${TBB_DIR}/../bin/intel64/vc14 ABSOLUTE)
  elseif(UNIX)
    get_filename_component(_tbb_lib_dir ${TBB_DIR}/../lib/intel64/gcc4.7 ABSOLUTE)
    get_filename_component(_tbb_bin_dir ${TBB_DIR}/../bin/intel64/gcc4.7 ABSOLUTE)
  endif()

  set(TBB_LIBRARY_DIR ${_tbb_lib_dir} CACHE PATH "The directory containing the TBB libraries")
  set(TBB_BINARY_DIR ${_tbb_bin_dir} CACHE PATH "The directory containing the TBB binaries")

  unset(_tbb_url)
  unset(_tbb_rel)
  unset(_tbb_fn)
  unset(_tbb_prefix)

  #else(WIN32)
  #
  #  set(_tbb_git_tag 2018_U5)
  #
  #  FetchContent_Declare(tbb
  #    GIT_REPOSITORY https://github.com/01org/tbb
  #    GIT_SHALLOW TRUE GIT_TAG ${_tbb_git_tag}
  #  )
  #
  #  FetchContent_GetProperties(tbb)
  #  if(NOT tbb_POPULATED)
  #    message(STATUS "Populating build dependency: tbb")
  #    FetchContent_Populate(tbb)
  #    include(${tbb_SOURCE_DIR}/cmake/TBBBuild.cmake)
  #
  #    if(CMAKE_CXX_COMPILER_ID STREQUAL Clang)
  #      tbb_build(TBB_ROOT ${tbb_SOURCE_DIR} CONFIG_DIR TBB_DIR
  #        MAKE_ARGS compiler=${CMAKE_C_COMPILER} tbb_build_prefix= tbb_build_dir=${tbb_BINARY_DIR})
  #    else()
  #      tbb_build(TBB_ROOT ${tbb_SOURCE_DIR} CONFIG_DIR TBB_DIR
  #        MAKE_ARGS tbb_build_prefix= tbb_build_dir=${tbb_BINARY_DIR})
  #    endif()
  #
  #    set(TBB_DIR ${TBB_DIR} PARENT_SCOPE)
  #  endif()
  #
  #  unset(_tbb_git_tag)
  #
  #endif()

