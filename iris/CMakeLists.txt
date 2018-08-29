find_package(Filesystem REQUIRED)
find_package(Threads REQUIRED)
find_package(TBB REQUIRED)
find_package(X11 REQUIRED)
find_package(Vulkan REQUIRED)
get_filename_component(Vulkan_LIBRARY_DIR ${Vulkan_LIBRARY} DIRECTORY CACHE)
get_filename_component(Vulkan_SDK_DIR ${Vulkan_LIBRARY_DIR}/.. ABSOLUTE CACHE)

find_package(PythonInterp 3 REQUIRED)
FetchContent_GetProperties(flextgl)

add_custom_command(
  OUTPUT
    ${CMAKE_CURRENT_BINARY_DIR}/flextVk.h
    ${CMAKE_CURRENT_BINARY_DIR}/flextVk.cpp
  COMMAND
    ${PYTHON_EXECUTABLE}
      ${flextgl_SOURCE_DIR}/flextGLgen.py
      -D.
      -t${CMAKE_CURRENT_SOURCE_DIR}/flextvk_template
      ${CMAKE_CURRENT_SOURCE_DIR}/flextvk_profile.txt
  DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/flextvk_profile.txt
    ${CMAKE_CURRENT_SOURCE_DIR}/flextvk_template/flextVk.h.template
    ${CMAKE_CURRENT_SOURCE_DIR}/flextvk_template/flextVk.cpp.template
)

if(WIN32)
  set(PLATFORM_WINDOWS 1)
  set(PLATFORM_LINUX 0)
elseif(UNIX)
  set(PLATFORM_WINDOWS 0)
  set(PLATFORM_LINUX 1)
else()
  set(PLATFORM_WINDOWS 0)
  set(PLATFORM_LINUX 0)
endif()

configure_file(config.h.in config.h @ONLY)

set(SOURCES
  ${CMAKE_CURRENT_BINARY_DIR}/flextVk.cpp
  wsi/window.cc
  wsi/window_x11.cc
  renderer/renderer.cc
)

add_library(iris ${SOURCES})

target_link_libraries(iris
  PUBLIC
    gsl glm expected
  PRIVATE
    spdlog
    absl::strings
    absl::container
    Vulkan::Vulkan ${X11_LIBRARIES}
    TBB::tbb Threads::Threads
)

target_include_directories(iris
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
    $<INSTALL_INTERFACE:${INCLUDE_DIR}>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_compile_definitions(iris
  PRIVATE
    $<$<CONFIG:Release>:NDEBUG>
    VK_USE_PLATFORM_XLIB_KHR VK_USE_PLATFORM_XLIB_XRANDR_EXT
)

target_compile_options(iris
  PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:-mavx -Wall -Wextra -Werror>
    $<$<CONFIG:Debug>:-g -Og>
    $<$<CONFIG:Release>:-O2>
)

add_executable(skel skel.cc)
target_link_libraries(skel iris absl::failure_signal_handler spdlog)
