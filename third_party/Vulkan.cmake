set(Vulkan_VERSION 1.1.101.0)

find_package(Vulkan ${Vulkan_VERSION} EXACT REQUIRED)
set_target_properties(Vulkan::Vulkan PROPERTIES IMPORTED_GLOBAL TRUE)

get_filename_component(Vulkan_LIBRARY_DIR ${Vulkan_LIBRARY} DIRECTORY CACHE)
get_filename_component(Vulkan_SDK_DIR ${Vulkan_LIBRARY_DIR}/.. ABSOLUTE CACHE)
