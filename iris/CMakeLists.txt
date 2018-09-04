#find_package(Filesystem REQUIRED)
find_package(Threads REQUIRED)
find_package(TBB REQUIRED)
find_package(X11 REQUIRED)

find_package(PythonInterp 3 REQUIRED)
FetchContent_GetProperties(flextgl)

if(WIN32)
  set(_flextvk_profile flextvk_profile_win32.txt)
else()
  set(_flextvk_profile flextvk_profile_xlib.txt)
endif()

add_custom_command(
  OUTPUT
    ${CMAKE_CURRENT_BINARY_DIR}/flextVk.h
    ${CMAKE_CURRENT_BINARY_DIR}/flextVk.cpp
  COMMAND
    ${PYTHON_EXECUTABLE}
      ${flextgl_SOURCE_DIR}/flextGLgen.py
      -D.
      -t${CMAKE_CURRENT_SOURCE_DIR}/flextvk_template
      ${CMAKE_CURRENT_SOURCE_DIR}/${_flextvk_profile}
  DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/${_flextvk_profile}
    ${CMAKE_CURRENT_SOURCE_DIR}/flextvk_template/flextVk.h.template
    ${CMAKE_CURRENT_SOURCE_DIR}/flextvk_template/flextVk.cpp.template
)

if(WIN32)
  set(PLATFORM_WINDOWS TRUE)
elseif(UNIX)
  set(PLATFORM_LINUX TRUE)
endif()

if(MSVC)
  set(PLATFORM_COMPILER_MSVC TRUE)
endif()

configure_file(config.h.in config.h)

set(SOURCES
  ${CMAKE_CURRENT_BINARY_DIR}/flextVk.cpp
  wsi/window.cc
  renderer/renderer.cc
  dso/dso.cc
  dso/desktop_window.cc
)

if(WIN32)
  list(APPEND SOURCES wsi/window_win32.cc)
elseif(UNIX)
  list(APPEND SOURCES wsi/window_x11.cc)
endif()

add_library(iris ${SOURCES})

target_link_libraries(iris
  PUBLIC
    flags imgui vma
    gsl glm expected
    absl::base absl::strings
    spdlog
    absl::container
    Vulkan::Vulkan ${X11_LIBRARIES}
    TBB::tbb Threads::Threads
)

target_include_directories(iris
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/..>
    $<INSTALL_INTERFACE:${INCLUDE_DIR}>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_compile_definitions(iris
  PUBLIC
    $<$<PLATFORM_ID:Windows>:WIN32_LEAN_AND_MEAN NOMINMAX>
  PRIVATE
    $<$<CONFIG:Release>:NDEBUG>
    $<$<PLATFORM_ID:Linux>:VK_USE_PLATFORM_XLIB_KHR VK_USE_PLATFORM_XLIB_XRANDR_EXT>
    $<$<PLATFORM_ID:Windows>:VK_USE_PLATFORM_WIN32_KHR>
)

target_compile_options(iris
  PUBLIC
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:-mavx -Wall -Wextra -Werror>
    $<$<AND:$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>,$<CONFIG:Debug>>:-g -Og>
    $<$<AND:$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>,$<CONFIG:Release>>:O2>
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX /wd4127 /wd4201 /wd4996>
    $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Debug>>:/ZI /Od /Gm>
    $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Release>>:/O2 /GL /Gw>
)

add_executable(skel skel.cc)
target_link_libraries(skel iris absl::failure_signal_handler)
