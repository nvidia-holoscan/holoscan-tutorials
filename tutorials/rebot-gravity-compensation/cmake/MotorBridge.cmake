# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Build the unmodified motorbridge v0.5.0 C ABI.
include(ExternalProject)
find_program(REBOT_CARGO_EXECUTABLE cargo REQUIRED)
find_package(Git REQUIRED)
set(REBOT_MOTORBRIDGE_COMMIT "082cbb8c36dfdc8fbf9baa4cc02c5992ab0dc2ad")
set(_rebot_motor_target "${CMAKE_BINARY_DIR}/motorbridge-cargo")
set(_rebot_cargo_command "${REBOT_CARGO_EXECUTABLE}")
set(_rebot_cargo_target_args)
set(_rebot_motor_output "${_rebot_motor_target}/release")
if(CMAKE_CROSSCOMPILING)
  if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR NOT CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
    message(FATAL_ERROR "This example supports cross-compiling for Linux ARM64 only")
  endif()
  set(_rebot_cargo_target_args --target aarch64-unknown-linux-gnu)
  set(_rebot_motor_output "${_rebot_motor_target}/aarch64-unknown-linux-gnu/release")
  # Cargo does not inherit CMake's target architecture or linker selection.
  set(_rebot_cargo_command "${CMAKE_COMMAND}" -E env
      "CARGO_TARGET_AARCH64_UNKNOWN_LINUX_GNU_LINKER=${CMAKE_CXX_COMPILER}"
      "${REBOT_CARGO_EXECUTABLE}")
endif()
set(REBOT_MOTOR_LIBRARY "${_rebot_motor_output}/${CMAKE_SHARED_LIBRARY_PREFIX}motor_abi${CMAKE_SHARED_LIBRARY_SUFFIX}"
    CACHE FILEPATH "Unmodified motorbridge runtime built from pinned source" FORCE)
set(REBOT_MOTORBRIDGE_SOURCE "${CMAKE_BINARY_DIR}/_deps/motorbridge")
ExternalProject_Add(rebot_motorbridge_external
  GIT_REPOSITORY https://github.com/motorbridge/motorbridge.git
  GIT_TAG "${REBOT_MOTORBRIDGE_COMMIT}"
  GIT_SHALLOW FALSE
  UPDATE_DISCONNECTED TRUE
  SOURCE_DIR "${REBOT_MOTORBRIDGE_SOURCE}"
  BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/motorbridge-build"
  CONFIGURE_COMMAND ""
  BUILD_COMMAND ${_rebot_cargo_command} build --manifest-path <SOURCE_DIR>/Cargo.toml
      --package motor_abi --release --locked --target-dir "${_rebot_motor_target}" ${_rebot_cargo_target_args}
  BUILD_BYPRODUCTS "${REBOT_MOTOR_LIBRARY}"
  INSTALL_COMMAND "")
