set(_vulkanmemoryallocator_git_tag v2.2.0)

message(STATUS "Populating build dependency: VulkanMemoryAllocator")
FetchContent_Populate(VulkanMemoryAllocator
  GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator
  GIT_SHALLOW TRUE GIT_TAG ${_vulkanmemoryallocator_git_tag}
  UPDATE_DISCONNECTED ${THIRD_PARTY_UPDATE_DISCONNECTED}
  QUIET
)

if(NOT EXISTS ${vulkanmemoryallocator_BINARY_DIR}/vk_mem_alloc.cc)
  file(WRITE ${vulkanmemoryallocator_BINARY_DIR}/vk_mem_alloc.cc
        "#define VMA_IMPLEMENTATION\n#include \"vk_mem_alloc.h\"")
endif()

add_library(vma ${vulkanmemoryallocator_BINARY_DIR}/vk_mem_alloc.cc)
target_include_directories(vma
  PUBLIC
    ${vulkanmemoryallocator_SOURCE_DIR}/src
    ${VulkanHeaders_INCLUDE_DIR}
)
target_link_libraries(vma PUBLIC Vulkan::Vulkan)
target_compile_definitions(vma
  PUBLIC
    $<$<PLATFORM_ID:Windows>:WIN32_LEAN_AND_MEAN>
  PRIVATE
    $<$<PLATFORM_ID:Linux>:VK_USE_PLATFORM_XCB_KHR VK_USE_PLATFORM_XCB_XRANDR_EXT>
    $<$<PLATFORM_ID:Windows>:VK_USE_PLATFORM_WIN32_KHR>
)
