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

set(TBB_LIBRARY_DIR ${_tbb_lib_dir}
  CACHE PATH "The directory containing the TBB libraries" FORCE)
set(TBB_BINARY_DIR ${_tbb_bin_dir}
  CACHE PATH "The directory containing the TBB binaries" FORCE)
