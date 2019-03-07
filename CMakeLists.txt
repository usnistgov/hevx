cmake_minimum_required(VERSION 3.11)
cmake_policy(VERSION 3.11)

set(CMAKE_USER_MAKE_RULES_OVERRIDE
    "${CMAKE_CURRENT_LIST_DIR}/cmake/HEVUserMakeRulesOverride.cmake")
project(hevx VERSION 3.0.0 LANGUAGES C CXX)

##
# CMake helpers
##
include(CMakeDependentOption)
list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)

##
# Build options
##
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
option(BUILD_DOCS "Build the documentation" ON)
CMAKE_DEPENDENT_OPTION(BUILD_DOCS_INTERNAL "Build developer documentation"
  ON "BUILD_DOCS" OFF)

# CTest adds a BUILD_TESTING option (default: ON) and calls enable_testing
if(PROJECT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
  include(CTest)
endif()

CMAKE_DEPENDENT_OPTION(BUILD_DEPENDENCY_TESTING "Build tests in dependencies"
  ON "BUILD_TESTING" OFF)

option(THIRD_PARTY_UPDATE_DISCONNECTED
  "Disable updating third-party repositories" OFF)

##
# HEVx configuration
##
include(HEVToolchain)
include(GNUInstallDirs)

##
# Unit testing
##
if(BUILD_TESTING)
  enable_testing()
endif()

##
# Required libraries
##
find_package(PythonInterp 3 REQUIRED)
set(Vulkan_VERSION 1.1.101.0)
find_package(Vulkan ${Vulkan_VERSION} EXACT REQUIRED)
get_filename_component(Vulkan_LIBRARY_DIR ${Vulkan_LIBRARY} DIRECTORY CACHE)
get_filename_component(Vulkan_SDK_DIR ${Vulkan_LIBRARY_DIR}/.. ABSOLUTE CACHE)

add_subdirectory(third_party)

##
# HEV components
##
add_subdirectory(iris)

##
# HEV Documentation
##
if(BUILD_DOCS AND (PROJECT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR))
  add_subdirectory(docs)
endif()
