cmake_minimum_required(VERSION 3.12)
cmake_policy(VERSION 3.12)

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
option(THIRD_PART_UPDATE_DICONNECTED "Update third party dependencies" ON)

# CTest adds a BUILD_TESTING option (default: ON) and calls enable_testing
if(PROJECT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
  include(CTest)
endif()

CMAKE_DEPENDENT_OPTION(BUILD_DEPENDENCY_TESTING "Build tests in dependencies"
  ON "BUILD_TESTING" OFF)

##
# HEVx configuration
##
include(HEVToolchain)
include(GNUInstallDirs)

##
# Packages we need in CMake
##
find_package(Python3 COMPONENTS Interpreter REQUIRED)
find_package(Git REQUIRED)

##
# Required libraries
##
add_subdirectory(third_party)

##
# HEV Documentation
##
if(BUILD_DOCS AND (PROJECT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR))
  add_subdirectory(docs)
endif()

##
# Unit testing
##
if(BUILD_TESTING)
  enable_testing()
endif()

##
# HEV components
##
add_subdirectory(iris)
