#
# Modified to first download the SDK on Linux
#
if(NOT WIN32 AND NOT "$ENV{VULKAN_SDK}")
  if (NOT DEFINED VulkanSDK_FIND_VERSION)
    message(FATAL_ERROR "No VulkanSDK version specified for downloading")
  endif()

  set(_file "vulkansdk-linux-x86_64-${VulkanSDK_FIND_VERSION}.tar.gz")
  set(_url "https://sdk.lunarg.com/sdk/download/${VulkanSDK_FIND_VERSION}/linux/${_file}?u=")

  if (NOT EXISTS ${CMAKE_CURRENT_BINARY_DIR}/${_file})
    message(STATUS "Downloading Vulkan SDK v${VulkanSDK_FIND_VERSION}")
    file(DOWNLOAD ${_url} ${CMAKE_CURRENT_BINARY_DIR}/${_file})
  endif()

  if(NOT EXISTS ${CMAKE_CURRENT_BINARY_DIR}/${VulkanSDK_FIND_VERSION})
    execute_process(COMMAND ${CMAKE_COMMAND} -E tar xf ${_file}
      WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
  endif()

  set(ENV{VULKAN_SDK} "${CMAKE_CURRENT_BINARY_DIR}/${VulkanSDK_FIND_VERSION}/x86_64")

  unset(_url)
  unset(_file)
endif()

# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

#.rst:
# FindVulkan
# ----------
#
# Try to find Vulkan
#
# IMPORTED Targets
# ^^^^^^^^^^^^^^^^
#
# This module defines :prop_tgt:`IMPORTED` target ``Vulkan::Vulkan``, if
# Vulkan has been found.
#
# Result Variables
# ^^^^^^^^^^^^^^^^
#
# This module defines the following variables::
#
#   Vulkan_FOUND          - True if Vulkan was found
#   Vulkan_INCLUDE_DIRS   - include directories for Vulkan
#   Vulkan_LIBRARIES      - link against this library to use Vulkan
#
# The module will also define two cache variables::
#
#   Vulkan_INCLUDE_DIR    - the Vulkan include directory
#   Vulkan_LIBRARY        - the path to the Vulkan library
#

if(WIN32)
  find_path(Vulkan_INCLUDE_DIR
    NAMES vulkan/vulkan.h
    PATHS
      "$ENV{VULKAN_SDK}/Include"
    )

  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    find_library(Vulkan_LIBRARY
      NAMES vulkan-1
      PATHS
        "$ENV{VULKAN_SDK}/Lib"
        "$ENV{VULKAN_SDK}/Bin"
        )
  elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
    find_library(Vulkan_LIBRARY
      NAMES vulkan-1
      PATHS
        "$ENV{VULKAN_SDK}/Lib32"
        "$ENV{VULKAN_SDK}/Bin32"
        NO_SYSTEM_ENVIRONMENT_PATH
        )
  endif()
else()
    find_path(Vulkan_INCLUDE_DIR
      NAMES vulkan/vulkan.h
      PATHS
        "$ENV{VULKAN_SDK}/include")
    find_library(Vulkan_LIBRARY
      NAMES vulkan
      PATHS
        "$ENV{VULKAN_SDK}/lib")
endif()

set(Vulkan_LIBRARIES ${Vulkan_LIBRARY})
set(Vulkan_INCLUDE_DIRS ${Vulkan_INCLUDE_DIR})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Vulkan
  DEFAULT_MSG
  Vulkan_LIBRARY Vulkan_INCLUDE_DIR)

mark_as_advanced(Vulkan_INCLUDE_DIR Vulkan_LIBRARY)

if(Vulkan_FOUND AND NOT TARGET Vulkan::Vulkan)
  add_library(Vulkan::Vulkan UNKNOWN IMPORTED)
  set_target_properties(Vulkan::Vulkan PROPERTIES
    IMPORTED_LOCATION "${Vulkan_LIBRARIES}"
    INTERFACE_INCLUDE_DIRECTORIES "${Vulkan_INCLUDE_DIRS}")
endif()
