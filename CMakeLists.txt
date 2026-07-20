#=============================================================================
# GASNet-EX CMake Build System
#=============================================================================
cmake_minimum_required(VERSION 3.16)

#---------------------------------------------------------------------
# Project definition
#---------------------------------------------------------------------
project(GASNet
  VERSION 2025.8.1
  DESCRIPTION "Global-Address Space Networking (GASNet-EX) communication system"
  HOMEPAGE_URL "https://gasnet.lbl.gov"
  LANGUAGES C CXX
)

include(GNUInstallDirs)

#---------------------------------------------------------------------
# Version numbers (from configure.in)
#---------------------------------------------------------------------
set(GASNET_RELEASE_VERSION_MAJOR 2025)
set(GASNET_RELEASE_VERSION_MINOR 8)
set(GASNET_RELEASE_VERSION_PATCH 1)

set(GASNETEX_SPEC_VERSION_MAJOR 0)
set(GASNETEX_SPEC_VERSION_MINOR 19)

set(GASNET_SPEC_VERSION_MAJOR 1)
set(GASNET_SPEC_VERSION_MINOR 8)

set(GASNETT_SPEC_VERSION_MAJOR 1)
set(GASNETT_SPEC_VERSION_MINOR 20)

set(GASNET_RELEASE_VERSION "${GASNET_RELEASE_VERSION_MAJOR}.${GASNET_RELEASE_VERSION_MINOR}.${GASNET_RELEASE_VERSION_PATCH}")

#---------------------------------------------------------------------
# CMake module path
#---------------------------------------------------------------------
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

#---------------------------------------------------------------------
# Build type and defaults
#---------------------------------------------------------------------
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

#---------------------------------------------------------------------
# Installation paths
#---------------------------------------------------------------------
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set(CMAKE_INSTALL_PREFIX "/usr/local/gasnet" CACHE PATH "Install prefix" FORCE)
endif()

#---------------------------------------------------------------------
# Top-level options
#---------------------------------------------------------------------
include(GasNetOptions)

#---------------------------------------------------------------------
# Platform and compiler detection
#---------------------------------------------------------------------
include(GasNetPlatform)
include(GasNetCompiler)

#---------------------------------------------------------------------
# Feature detection
#---------------------------------------------------------------------
include(GasNetPthreads)
include(GasNetAtomics)
include(GasNetTimer)
include(GasNetMembar)
include(GasNetDebugMalloc)

#---------------------------------------------------------------------
# PSHM support
#---------------------------------------------------------------------
include(GasNetPSHM)

#---------------------------------------------------------------------
# Memory kinds support
#---------------------------------------------------------------------
include(GasNetMemoryKinds)

#---------------------------------------------------------------------
# Generate gasnet_config.h
#---------------------------------------------------------------------
include(GasNetConfigHeader)

#---------------------------------------------------------------------
# Build gasnet_tools library (conduit-independent)
#---------------------------------------------------------------------
add_subdirectory(other)

#---------------------------------------------------------------------
# Build extended-ref library objects
#---------------------------------------------------------------------
add_subdirectory(extended-ref)

#---------------------------------------------------------------------
# Build conduits
#---------------------------------------------------------------------
add_subdirectory(conduits)

#---------------------------------------------------------------------
# Build tests
#---------------------------------------------------------------------
if(GASNET_ENABLE_TESTS)
  enable_testing()
  # Tests disabled by default during CMake port - enable when conduit libs stabilize
  # add_subdirectory(tests)
endif()

#---------------------------------------------------------------------
# Install
#---------------------------------------------------------------------
include(GasNetInstall)
