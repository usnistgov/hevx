set(_vulkanloader_git_tag sdk-1.1.97.0)

message(STATUS "Populating build dependency: VulkanLoader")
FetchContent_Populate(VulkanLoader
  GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Loader
  GIT_SHALLOW TRUE GIT_TAG ${_vulkanloader_git_tag}
  CMAKE_CACHE_ARGS
    -DVulkanHeaders_FOUND:BOOL=TRUE
    -DVulkanHeaders_INCLUDE_DIR:STRING=${VULKAN_HEADERS_INSTALL_DIR}
    -DVulkanRegistry_FOUND:BOOL=TRUE
    -DVulkanRegistry_DIR:STRING=${VULKAN_HEADERS_INSTALL_DIR}/../registry
  QUIET
)

set(VulkanHeaders_FOUND TRUE)
set(VulkanHeaders_INCLUDE_DIR ${VULKAN_HEADERS_INSTALL_DIR})
set(VulkanRegistry_FOUND TRUE)
set(VulkanRegistry_DIR ${VULKAN_HEADERS_INSTALL_DIR}/../registry)
add_subdirectory(${vulkanloader_SOURCE_DIR} ${vulkanloader_BINARY_DIR})
