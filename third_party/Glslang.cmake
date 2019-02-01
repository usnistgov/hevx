set(BUILD_TESTING OFF)
message(STATUS "Populating glslang")
add_subdirectory(${Vulkan_SDK_DIR}/../source/glslang ${CMAKE_CURRENT_BINARY_DIR}/glslang)
