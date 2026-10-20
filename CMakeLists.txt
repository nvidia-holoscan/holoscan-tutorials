# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 4.0)
project(HoloscanTutorials LANGUAGES NONE)

option(BUILD_ALL "Build all registered tutorials" OFF)

# CLI containers run as the invoking user. Stage installs in the mounted build
# directory unless the caller selected another prefix.
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/install" CACHE PATH "Installation directory" FORCE)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(HoloscanTutorialsApplication)
add_subdirectory(tutorials)

# The CLI selects a project with -DAPP_<name>=ON. Reject a discovered tutorial
# without a registered CMake project rather than reporting an empty build.
get_property(_registered_apps GLOBAL PROPERTY HOLOSCAN_TUTORIAL_APPLICATIONS)
get_cmake_property(_cache_variables CACHE_VARIABLES)
foreach(_variable IN LISTS _cache_variables)
  if(_variable MATCHES "^APP_")
    if(${_variable})
      list(FIND _registered_apps "${_variable}" _registered_index)
      if(_registered_index EQUAL -1)
        message(FATAL_ERROR "${_variable} has no CMake application registered in tutorials/CMakeLists.txt")
      endif()
    endif()
  endif()
endforeach()
