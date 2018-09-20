cmake_minimum_required(VERSION 3.11)
set(CMAKE_USER_MAKE_RULES_OVERRIDE
    "${CMAKE_CURRENT_LIST_DIR}/cmake/HEVUserMakeRulesOverride.cmake")
project(hevx VERSION 3.0.0 LANGUAGES C CXX)

##
# Build options
##
option(BUILD_TESTING "Build the testing tree" ON)
option(BUILD_DOCS "Build the documentation" ON)
option(BUILD_DOCS_INTERNAL "Build the internal developer documentation" ON)

##
# CMake configuration
##
list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)
include(HEVToolchain)
include(GNUInstallDirs)

##
# Unit testing
##
if(BUILD_TESTING AND (PROJECT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR))
  enable_testing()
  #add_subdirectory(test)
endif()

##
# Required libraries and third-party components
##
find_package(Vulkan REQUIRED)
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
