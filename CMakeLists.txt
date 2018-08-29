cmake_minimum_required(VERSION 2.8)

project(otagent)

find_package(Git)
if(GIT_FOUND)
    execute_process(COMMAND git "describe" OUTPUT_VARIABLE GIT_VERSION WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} OUTPUT_STRIP_TRAILING_WHITESPACE)
    string(REGEX REPLACE "^v*([0-9]+)\\..*" "\\1" VERSION_MAJOR "${GIT_VERSION}")
    string(REGEX REPLACE "^v*[0-9]+\\.([0-9]+).*" "\\1" VERSION_MINOR "${GIT_VERSION}")
    string(REGEX REPLACE "^v*[0-9]+\\.[0-9]+\\.([0-9]+).*" "\\1" VERSION_BUGFIX "${GIT_VERSION}")
    string(REGEX REPLACE "^v*[0-9]+\\.[0-9]+\\.[0-9]+-([0-9]+)-.*" "\\1" VERSION_NEW_COMMITS "${GIT_VERSION}")
    string(REGEX REPLACE "^v*[0-9]+\\.[0-9]+\\.[0-9]+-[0-9]+-(.*)" "\\1" VERSION_SHA1 "${GIT_VERSION}")
    if(("${VERSION_SHA1}" STREQUAL "") OR ("${VERSION_SHA1}" STREQUAL "${GIT_VERSION}"))
        set(VERSION_STRING "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_BUGFIX}")
    else()
        set(VERSION_STRING "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_BUGFIX}-${VERSION_NEW_COMMITS}-${VERSION_SHA1}")
    endif()
else()
  message(FATAL_ERROR "Git not found.")
endif()


#-----------------------------------------------------------------------------
# Options for building

option(BUILD_TESTS         "Build the unit tests." ON)

option(BUILD_VERBOSE       "Verbose build output." ON)

set(PACKAGE_CONTACT        ""              CACHE <TYPE>  "Package Maintainer")
set(PACKAGE_VENDOR         "Stash Crypto"  CACHE <TYPE>  "Package Vendor")

if(BUILD_VERBOSE)
  set(CMAKE_VERBOSE_MAKEFILE true)
endif()

#-----------------------------------------------------------------------------
# Print system information

message(STATUS "otagent version:   			  ${VERSION_STRING}")
message(STATUS "CMake version:                ${CMAKE_VERSION}")
message(STATUS "System:                       ${CMAKE_SYSTEM}")
message(STATUS "Processor:                    ${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "Verbose:                      ${BUILD_VERBOSE}")
message(STATUS "Package Contact:              ${PACKAGE_CONTACT}")
message(STATUS "Package Vendor:               ${PACKAGE_VENDOR}")

#-----------------------------------------------------------------------------
# Collect all binaries into bin subdirectory

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_CURRENT_BINARY_DIR}/bin)

#-----------------------------------------------------------------------------
# Check which compiler to use

if(${CMAKE_CXX_COMPILER_ID} MATCHES GNU)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17")
  set(COMPILER_EXTRA_FLAGS "")
elseif(${CMAKE_CXX_COMPILER_ID} MATCHES Clang)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++1z")
  set(COMPILER_EXTRA_FLAGS "-Weverything -Wno-c++98-compat -Wno-c++98-compat-pedantic -Wno-padded -Wno-weak-vtables -Wno-disabled-macro-expansion -Wno-sign-conversion -Wno-float-conversion -Wno-unused-private-field -Wno-covered-switch-default")
else()
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17")
  set(COMPILER_EXTRA_FLAGS "")
endif()

set(PEDANTIC_CXX_FLAGS "-Wall -Werror -Wextra -pedantic -pedantic-errors -Wswitch-enum -Wmissing-braces -Wcast-align -Wctor-dtor-privacy -Wdisabled-optimization -Winit-self -Wswitch-default -Wold-style-cast -Woverloaded-virtual -Wmissing-declarations -Wunused-macros -Wundef -Wformat=2 -Wno-format-nonliteral ${COMPILER_EXTRA_FLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${PEDANTIC_CXX_FLAGS}")

message(STATUS "C++ Compiler ID: ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "C++ Flags:       ${CMAKE_CXX_FLAGS}")
message(STATUS "C++ link flags:  ${CMAKE_CXX_LINK_FLAGS}")

#-----------------------------------------------------------------------------
# System libraries used for linking.

if(UNIX AND NOT APPLE)
  list(APPEND APP_SYSTEM_LIBRARIES rt)
endif()
list(APPEND APP_SYSTEM_LIBRARIES dl)

#-----------------------------------------------------------------------------
# Dependencies

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
find_package(Protobuf REQUIRED)
find_package(opentxs-proto REQUIRED)
find_package(opentxs REQUIRED)
find_package(Boost REQUIRED program_options filesystem system)

if(BUILD_TESTS)
  find_package(GTest REQUIRED)
  enable_testing()
endif()

#-----------------------------------------------------------------------------
# Force out-of-source build

if(${CMAKE_CURRENT_SOURCE_DIR} STREQUAL ${CMAKE_CURRENT_BINARY_DIR})
message(FATAL_ERROR "CMake generation is not allowed within the source directory!
Remove the CMakeCache.txt file and try again from another folder, e.g.:

   rm CMakeCache.txt
   mkdir build
   cd build
   cmake ..
")
endif()


#-----------------------------------------------------------------------------
# Default to Debug build

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING
      "Choose the type of build, options are: None Debug Release RelWithDebInfo MinSizeRel."
      FORCE)
endif()

#-----------------------------------------------------------------------------
# Build source

#Fix RPATH
include(GNUInstallDirs)
SET(CMAKE_SKIP_BUILD_RPATH  FALSE)
set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")

LIST(APPEND CMAKE_PREFIX_PATH "/usr/share/cmake/Modules")
LIST(APPEND CMAKE_PREFIX_PATH "/usr/local/share/cmake/Modules")


add_definitions(-D_XOPEN_SOURCE=700)

#include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

add_subdirectory(src)


#-----------------------------------------------------------------------------
# Build Unit tests

if(GTEST_FOUND)
  add_subdirectory(tests)
endif()

#-----------------------------------------------------------------------------
# Uninstall
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/uninstall.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/uninstall.cmake"
    IMMEDIATE @ONLY)

add_custom_target(uninstall
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/uninstall.cmake)

