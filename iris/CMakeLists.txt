find_package(Filesystem REQUIRED)
find_package(Threads REQUIRED)
find_package(TBB REQUIRED)
find_package(X11 REQUIRED)

find_package(PythonInterp 3 REQUIRED)
FetchContent_GetProperties(flextgl)

set(protos_PROTOS
  protos/color.proto
  protos/control.proto
  protos/displays.proto
  protos/window.proto
)

foreach(filename ${protos_PROTOS})
  get_filename_component(dirname ${filename} DIRECTORY)
  get_filename_component(basename ${filename} NAME_WE)
  list(APPEND protos_SOURCES ${CMAKE_CURRENT_BINARY_DIR}/${dirname}/${basename}.pb.cc)

  add_custom_command(
    OUTPUT
      ${CMAKE_CURRENT_BINARY_DIR}/${dirname}/${basename}.pb.h
      ${CMAKE_CURRENT_BINARY_DIR}/${dirname}/${basename}.pb.cc
    COMMAND protobuf::protoc
    ARGS
      --proto_path=${CMAKE_CURRENT_SOURCE_DIR}/protos
      --cpp_out=${CMAKE_CURRENT_BINARY_DIR}/protos
      ${CMAKE_CURRENT_SOURCE_DIR}/${filename}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${filename} protobuf::protoc
    COMMENT "Running cpp protocol buffer compiler on ${filename}"
    VERBATIM
  )
endforeach()

add_library(protos OBJECT ${protos_SOURCES})
target_include_directories(protos PRIVATE ${PROTOBUF_INCLUDE_DIR})

target_compile_options(protos
  PRIVATE
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:-Wall -Wnon-virtual-dtor -Wno-unused-parameter -Wno-maybe-uninitialized -Wsuggest-final-types>
    $<$<AND:$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>,$<CONFIG:Debug>>:-g -Og>
    $<$<AND:$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>,$<CONFIG:Release>>:-O2>
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /wd4100 /wd4125 /wd4127 /wd4201 /wd4996>
    $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Debug>>:/ZI /Od /Gm>
    $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Release>>:/O2 /GL /Gw>
)

if(WIN32)
  set(_flextvk_profile vk_win32_khr.txt)
else()
  set(_flextvk_profile vk_xlib_khr.txt)
endif()

add_custom_command(
  OUTPUT
    ${CMAKE_CURRENT_BINARY_DIR}/flextVk.h
    ${CMAKE_CURRENT_BINARY_DIR}/flextVk.cpp
  COMMAND ${PYTHON_EXECUTABLE} ${flextgl_SOURCE_DIR}/flextGLgen.py
  ARGS
      -D.
      -t${CMAKE_CURRENT_SOURCE_DIR}/flext/vulkan
      ${CMAKE_CURRENT_SOURCE_DIR}/flext/${_flextvk_profile}
  DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/flext/${_flextvk_profile}
    ${CMAKE_CURRENT_SOURCE_DIR}/flext/vulkan/flextVk.h.template
    ${CMAKE_CURRENT_SOURCE_DIR}/flext/vulkan/flextVk.cpp.template
)

add_custom_command(
  OUTPUT
    ${CMAKE_CURRENT_BINARY_DIR}/flextGL.h
    ${CMAKE_CURRENT_BINARY_DIR}/flextGL.c
  COMMAND ${PYTHON_EXECUTABLE} ${flextgl_SOURCE_DIR}/flextGLgen.py
  ARGS
      -D.
      -t${CMAKE_CURRENT_SOURCE_DIR}/flext/compatible
      ${CMAKE_CURRENT_SOURCE_DIR}/flext/gl_45_core.txt
  DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/flext/gl_45_core.txt
    ${CMAKE_CURRENT_SOURCE_DIR}/flext/compatible/flextGL.h.template
    ${CMAKE_CURRENT_SOURCE_DIR}/flext/compatible/flextGL.c.template
)

if(WIN32)
  set(PLATFORM_WINDOWS TRUE)
elseif(UNIX)
  set(PLATFORM_LINUX TRUE)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
  set(PLATFORM_COMPILER_MSVC TRUE)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  set(PLATFORM_COMPILER_GCC TRUE)
endif()

configure_file(config.h.in config.h)

set(SOURCES
  ${CMAKE_CURRENT_BINARY_DIR}/flextGL.c
  ${CMAKE_CURRENT_BINARY_DIR}/flextVk.cpp
  wsi/window.cc
  renderer/io.cc
  renderer/renderer.cc
  renderer/surface.cc
  renderer/window.cc
)

if(WIN32)
  list(APPEND SOURCES wsi/window_win32.cc)
elseif(UNIX)
  list(APPEND SOURCES wsi/window_x11.cc)
endif()

add_library(iris ${SOURCES} $<TARGET_OBJECTS:protos>)

target_link_libraries(iris
  PUBLIC
    flags imgui vma
    gsl glm expected CXX::Filesystem fmt spdlog
    absl::base absl::strings absl::container absl::hash
    shaderc OpenGL::GL Vulkan::Vulkan ${X11_LIBRARIES}
    protobuf::libprotobuf nng
    TBB::tbb Threads::Threads
    $<$<PLATFORM_ID:Linux>:-ldl>
)

target_include_directories(iris
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/..>
    $<BUILD_INTERFACE:${NNG_INCLUDE_DIR}>
    $<INSTALL_INTERFACE:${INCLUDE_DIR}>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/protos
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
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:-Wall -Wextra -Wshadow -Wnon-virtual-dtor -Werror>
    $<$<AND:$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>,$<CONFIG:Debug>>:-g -Og>
    $<$<AND:$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>,$<CONFIG:Release>>:O2>
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX /wd4127 /wd4201 /wd4996>
    $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Debug>>:/ZI /Od /Gm>
    $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Release>>:/O2 /GL /Gw>
)

add_executable(skel WIN32 skel.cc)
target_link_libraries(skel iris absl::failure_signal_handler)
if(WIN32)
  file(GLOB tbb_dlls ${TBB_BINARY_DIR}/*.dll)
  foreach(dll ${tbb_dlls})
    add_custom_command(TARGET skel POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy ${dll} $<TARGET_FILE_DIR:skel>)
  endforeach()
  unset(tbb_dlls)
endif()
