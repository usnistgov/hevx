set(_vulkanheaders_git_tag sdk-1.1.97.0)

message(STATUS "Populating build dependency: VulkanHeaders")
FetchContent_Populate(VulkanHeaders
  GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers
  GIT_SHALLOW TRUE GIT_TAG ${_vulkanheaders_git_tag}
  QUIET
)

add_subdirectory(${vulkanheaders_SOURCE_DIR} ${vulkanheaders_BINARY_DIR})
set(VULKAN_HEADERS_INSTALL_DIR "${vulkanheaders_SOURCE_DIR}/include"
  CACHE STRING "" FORCE)
