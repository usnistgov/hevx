FetchContent_Declare(vma
  GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator
  GIT_SHALLOW TRUE # VulkanMemoryAllocator "should" be stable at head
)

FetchContent_GetProperties(vma)
if(NOT vma_POPULATED)
  message(STATUS "Populating build dependency: vma")
  FetchContent_Populate(vma)

  if(NOT EXISTS ${CMAKE_CURRENT_BINARY_DIR}/vk_mem_alloc.cc)
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/vk_mem_alloc.cc
         "#define VMA_IMPLEMENTATION\n#include \"vk_mem_alloc.h\"")
  endif()

  add_library(vma ${CMAKE_CURRENT_BINARY_DIR}/vk_mem_alloc.cc)
  target_include_directories(vma PUBLIC ${vma_SOURCE_DIR}/src)
  target_link_libraries(vma PUBLIC Vulkan::Vulkan)
  target_compile_definitions(vma
    PUBLIC
      $<$<PLATFORM_ID:Windows>:WIN32_LEAN_AND_MEAN>
    PRIVATE
      $<$<PLATFORM_ID:Linux>:VK_USE_PLATFORM_XLIB_KHR VK_USE_PLATFORM_XLIB_XRANDR_EXT>
      $<$<PLATFORM_ID:Windows>:VK_USE_PLATFORM_WIN32_KHR>
  )
endif()
