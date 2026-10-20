# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

# Focused fork of add_holohub_application() from HoloHub's
# cmake/HoloHubConfigHelpers.cmake at f0435fb37a4c0abaafd94718dda90d4ec843dc56.
# Keep the APP_* and DEPENDS conventions so
# tutorials can use the same registration form without a HoloHub checkout.
function(add_holohub_application NAME)
  if(NOT NAME MATCHES "^[A-Za-z0-9_-]+$")
    message(FATAL_ERROR "Invalid tutorial application name: ${NAME}")
  endif()

  cmake_parse_arguments(APP "" "" "DEPENDS" ${ARGN})
  if(APP_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR "Unsupported arguments for ${NAME}: ${APP_UNPARSED_ARGUMENTS}")
  endif()

  set(appname "APP_${NAME}")
  option(${appname} "Build the ${NAME} application" ${BUILD_ALL})
  set_property(GLOBAL APPEND PROPERTY HOLOSCAN_TUTORIAL_APPLICATIONS "${appname}")

  if(NOT ${appname})
    return()
  endif()

  if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${NAME}/CMakeLists.txt")
    message(FATAL_ERROR "${appname} is enabled, but ${NAME}/CMakeLists.txt is missing")
  endif()
  add_subdirectory(${NAME})

  if(APP_DEPENDS)
    cmake_parse_arguments(DEPS "" "" "EXTENSIONS;OPERATORS" ${APP_DEPENDS})
    if(DEPS_UNPARSED_ARGUMENTS)
      message(FATAL_ERROR "Unsupported dependencies for ${NAME}: ${DEPS_UNPARSED_ARGUMENTS}")
    endif()

    foreach(dependency IN LISTS DEPS_EXTENSIONS)
      set("EXT_${dependency}" ON CACHE BOOL "Build the ${dependency}" FORCE)
    endforeach()

    unset(op_optional)
    foreach(dependency IN LISTS DEPS_OPERATORS)
      if(dependency STREQUAL "OPTIONAL")
        set(op_optional 1)
        continue()
      endif()

      if(op_optional)
        string(REPLACE "\"" "" holohub_build_operators "${HOLOHUB_BUILD_OPERATORS}")
        if(${dependency} IN_LIST holohub_build_operators)
          set("OP_${dependency}" ON CACHE BOOL "Build the ${dependency}" FORCE)
        endif()
      else()
        set("OP_${dependency}" ON CACHE BOOL "Build the ${dependency}" FORCE)
      endif()
    endforeach()
  endif()
endfunction()
