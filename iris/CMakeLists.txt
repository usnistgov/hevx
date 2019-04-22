find_package(Threads REQUIRED)
find_package(TBB REQUIRED)

if(UNIX)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(X11_XCB REQUIRED x11-xcb)
  pkg_check_modules(XCB_ICCCM REQUIRED xcb-icccm)
endif()

set(protos_PROTOS
  protos/color.proto
  protos/control.proto
  protos/displays.proto
  protos/shadertoy.proto
  protos/window.proto
)

file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/protos)
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
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:
      -Wall -Wnon-virtual-dtor
      -Wno-unused-parameter -Wno-maybe-uninitialized
    >
    $<$<AND:$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>,$<CONFIG:Debug>>:-g -Og>
    $<$<AND:$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>,$<CONFIG:Release>>:-O2>
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:
      /wd4100 # unreferenced format parameter
      /wd4125 # decimal digit terminates octal escape sequence
    >
    $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Debug>>:/Od>
    $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Release>>:/O2 /GL /Gw>
)

target_compile_definitions(protos
  PUBLIC
    $<$<PLATFORM_ID:Windows>:_SILENCE_CXX17_ITERATOR_BASE_CLASS_DEPRECATION_WARNING>
)

if(WIN32)
  set(_flextvk_profile vk_win32_khr.txt)
elseif(UNIX)
  set(_flextvk_profile vk_xcb_khr.txt)
endif()

add_custom_command(
  OUTPUT
    ${CMAKE_CURRENT_BINARY_DIR}/flextVk.h
    ${CMAKE_CURRENT_BINARY_DIR}/flextVk.cpp
  COMMAND ${Python3_EXECUTABLE} ${FLEXTGL_SOURCE_DIR}/flextGLgen.py
  ARGS
      -D.
      -t${CMAKE_CURRENT_SOURCE_DIR}/flext/vulkan
      ${CMAKE_CURRENT_SOURCE_DIR}/flext/${_flextvk_profile}
  DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/flext/${_flextvk_profile}
    ${CMAKE_CURRENT_SOURCE_DIR}/flext/vulkan/flextVk.h.template
    ${CMAKE_CURRENT_SOURCE_DIR}/flext/vulkan/flextVk.cpp.template
)

if(WIN32)
  set(PLATFORM_WINDOWS TRUE)
  set(PLATFORM_LINUX FALSE)
elseif(UNIX)
  set(PLATFORM_WINDOWS FALSE)
  set(PLATFORM_LINUX TRUE)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
  set(PLATFORM_COMPILER_MSVC TRUE)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  set(PLATFORM_COMPILER_GCC TRUE)
endif()

configure_file(config.h.in config.h)

set(SOURCES
  acceleration_structure.cc
  buffer.cc
  ${CMAKE_CURRENT_BINARY_DIR}/flextVk.h
  ${CMAKE_CURRENT_BINARY_DIR}/flextVk.cpp
  image.cc
  io/gltf.cc
  io/json.cc
  io/read_file.cc
  io/shadertoy.cc
  pipeline.cc
  renderer.cc
  shader.cc
  string_util.cc
  vulkan_util.cc
  window.cc
  wsi/platform_window.cc
)

if(WIN32)
  list(APPEND SOURCES wsi/platform_window_win32.cc)
elseif(UNIX)
  list(APPEND SOURCES wsi/platform_window_x11.cc)
endif()

add_library(iris STATIC ${SOURCES} $<TARGET_OBJECTS:protos>)

target_link_libraries(iris
  PUBLIC
    flags imgui vma
    GSL glm expected fmt spdlog nlohmann_json::nlohmann_json
    absl::base absl::hash absl::fixed_array absl::flat_hash_map
    mikktspace stb glslang SPIRV Vulkan::Vulkan
    ${X11_XCB_LIBRARIES} ${XCB_XINPUT_LIBRARIES} ${XCB_ICCCM_LIBRARIES}
    ${XCB_RANDR_LIBRARIES} ${XCB_XKB_LIBRARIES} ${XCB_UTIL_LIBRARIES}
    ${XCB_CURSOR_LIBRARIES}
    protobuf::libprotobuf
    cpprestsdk::cpprest
    TBB::tbb Threads::Threads
    $<$<CXX_COMPILER_ID:GNU>:-lstdc++fs>
    $<$<CXX_COMPILER_ID:Clang>:-lc++fs>
    $<$<PLATFORM_ID:Linux>:-ldl>
)

target_include_directories(iris
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/..>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/..>
    $<INSTALL_INTERFACE:${INCLUDE_DIR}>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/protos
)

target_compile_definitions(iris
  PUBLIC
    GLM_FORCE_RADIANS GLM_FORCE_DEPTH_ZERO_TO_ONE GLM_FORCE_EXPLICIT_CTOR GLM_FORCE_INLINE
    $<$<PLATFORM_ID:Linux>:VK_USE_PLATFORM_XCB_KHR VK_USE_PLATFORM_XCB_XRANDR_EXT>
    $<$<PLATFORM_ID:Windows>:VK_USE_PLATFORM_WIN32_KHR WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS>
    $<$<PLATFORM_ID:Windows>:_SILENCE_CXX17_ITERATOR_BASE_CLASS_DEPRECATION_WARNING>
    $<$<PLATFORM_ID:Windows>:_SILENCE_CXX17_OLD_ALLOCATOR_MEMBERS_DEPRECATION_WARNING>
  PRIVATE
    $<$<CONFIG:Release>:NDEBUG>
)

target_compile_options(iris
  PUBLIC
    $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:
      -Wall -Wextra -Wshadow -Wnon-virtual-dtor
      -Wno-maybe-uninitialized # used in protobuf
    >
    $<$<AND:$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>,$<CONFIG:Debug>>:-g -Og>
    $<$<AND:$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>,$<CONFIG:Release>>:-O2>
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX /permissive- /Zc:__cplusplus>
    $<$<CXX_COMPILER_ID:MSVC>:
      /wd4127 # conditional expression is constant
      /wd4201 # nonstandard extension used: nameless struct/union
      /wd4245 # signed/unsigned mismatch (used in absl/container/internal)
      /wd4324 # structure was padded due to alignment specifier (used in absl/container/internal)
    >
    $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Debug>>:/Od>
    $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Release>>:/O2 /GL /Gw>
)

add_executable(iris-viewer WIN32 iris-viewer.cc)
target_link_libraries(iris-viewer iris absl::failure_signal_handler)
if(WIN32)
  file(GLOB tbb_dlls ${TBB_BINARY_DIR}/*.dll)
  foreach(dll ${tbb_dlls})
    add_custom_command(TARGET iris-viewer POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy ${dll} $<TARGET_FILE_DIR:iris-viewer>)
  endforeach()
  unset(tbb_dlls)
endif()

add_executable(iris-raytracer WIN32 iris-raytracer.cc)
target_link_libraries(iris-raytracer iris absl::failure_signal_handler)
if(WIN32)
  file(GLOB tbb_dlls ${TBB_BINARY_DIR}/*.dll)
  foreach(dll ${tbb_dlls})
    add_custom_command(TARGET iris-raytracer POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy ${dll} $<TARGET_FILE_DIR:iris-raytracer>)
  endforeach()
  unset(tbb_dlls)
endif()
