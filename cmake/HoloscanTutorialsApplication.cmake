# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

# Focused fork of add_holohub_application() from HoloHub's
# cmake/HoloHubConfigHelpers.cmake at f0435fb37a4c0abaafd94718dda90d4ec843dc56.
# Keep the APP_* selection convention without requiring a HoloHub checkout.
function(add_holoscan_tutorial NAME)
  set(appname "APP_${NAME}")
  option(${appname} "Build the ${NAME} tutorial" OFF)

  if(NOT ${appname})
    return()
  endif()

  if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${NAME}/CMakeLists.txt")
    message(WARNING "${appname} is enabled, but ${NAME}/CMakeLists.txt is missing; skipping")
    return()
  endif()
  add_subdirectory(${NAME})
endfunction()
