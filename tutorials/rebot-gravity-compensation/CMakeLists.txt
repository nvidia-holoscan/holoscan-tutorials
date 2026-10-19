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

cmake_minimum_required(VERSION 4.0)
project(rebot_holoscan VERSION 0.1.0 LANGUAGES CXX)

include(CTest)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Use the SDK's installed package and schema generator, as in its C++ examples.
find_package(holoscan 5...<6 CONFIG REQUIRED COMPONENTS core config_io)

# bootstrap-deps.sh installs Pinocchio's native libraries into this local venv.
if(NOT CMAKE_CROSSCOMPILING)
  file(GLOB _pinocchio_prefixes
      "${CMAKE_CURRENT_SOURCE_DIR}/.venv/lib/python*/site-packages/cmeel.prefix")
  list(APPEND CMAKE_PREFIX_PATH ${_pinocchio_prefixes})
endif()
find_package(pinocchio 3.9 CONFIG REQUIRED)
find_package(Threads REQUIRED)
include(cmake/MotorBridge.cmake)

holoscan_add_flatbuffer_schema(
    TARGET joint_state_schema
    SCHEMA src/schemas/joint_state.fbs
    MAX_SERIALIZED_SIZE 4096)
holoscan_add_flatbuffer_schema(
    TARGET gravity_command_schema
    SCHEMA src/schemas/gravity_command.fbs
    MAX_SERIALIZED_SIZE 8192)

# Keep hardware and controller code in one internal library so tests use the
# same implementation as the application.
add_library(rebot_control STATIC
    src/robot.cpp
    src/dynamics.cpp
    src/control.cpp
    src/gravity_compensation.cpp
    src/robot_session.cpp
    src/operators/joint_state_source.cpp
    src/operators/pinocchio_gravity.cpp
    src/operators/motor_command_sink.cpp)
target_compile_features(rebot_control PUBLIC cxx_std_20)
target_include_directories(rebot_control PUBLIC include)
target_include_directories(rebot_control PRIVATE "${REBOT_MOTORBRIDGE_SOURCE}/motor_abi/include")
target_link_libraries(rebot_control PUBLIC
    holoscan::core holoscan::config_io holoscan::params
    joint_state_schema gravity_command_schema
    pinocchio::pinocchio Threads::Threads ${CMAKE_DL_LIBS})
target_compile_options(rebot_control PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(rebot_control PRIVATE
    REBOT_DEFAULT_MOTOR_LIBRARY="${REBOT_MOTOR_LIBRARY}")
add_dependencies(rebot_control rebot_motorbridge_external)

add_executable(rebot_gravity_compensation apps/gravity_compensation.cpp)
target_link_libraries(rebot_gravity_compensation PRIVATE rebot_control)

if(BUILD_TESTING)
  add_library(rebot_fake_motor_abi SHARED tests/fake_motor_abi.cpp)
  target_include_directories(rebot_fake_motor_abi PRIVATE "${REBOT_MOTORBRIDGE_SOURCE}/motor_abi/include")
  add_dependencies(rebot_fake_motor_abi rebot_motorbridge_external)
  target_compile_features(rebot_fake_motor_abi PRIVATE cxx_std_20)
  foreach(name robot dynamics control control_graph)
    add_executable(test_${name} tests/test_${name}.cpp)
    target_link_libraries(test_${name} PRIVATE rebot_control)
    target_compile_definitions(test_${name} PRIVATE
        REBOT_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        REBOT_TEST_MOTOR_LIBRARY="$<TARGET_FILE:rebot_fake_motor_abi>")
    add_dependencies(test_${name} rebot_fake_motor_abi)
    add_test(NAME ${name} COMMAND test_${name})
    set_tests_properties(${name} PROPERTIES TIMEOUT 20)
  endforeach()
  find_package(Python3 3.12 REQUIRED COMPONENTS Interpreter)
  add_test(NAME gravity_cli
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_cli.py"
          "$<TARGET_FILE:rebot_gravity_compensation>"
          "$<TARGET_FILE:rebot_fake_motor_abi>"
          "${CMAKE_CURRENT_SOURCE_DIR}/config/rebotarm_dm.yaml")
  set_tests_properties(gravity_cli PROPERTIES TIMEOUT 20)
endif()
