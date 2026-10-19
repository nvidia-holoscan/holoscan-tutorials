/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "rebot/dynamics.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string& message) {
  if (!condition) { throw std::runtime_error(message); }
}

void near(double actual, double expected, double tolerance, const char* label) {
  require(std::isfinite(actual) && std::abs(actual - expected) <= tolerance,
          std::string(label) + ": got " + std::to_string(actual) +
              ", expected " + std::to_string(expected));
}

template <typename Callable>
void invalid(Callable&& callable, const char* label) {
  try {
    callable();
  } catch (const std::invalid_argument&) {
    return;
  }
  throw std::runtime_error(std::string("expected invalid_argument: ") + label);
}

void check_analytic_pendulum() {
  const std::string path = std::string(REBOT_TEST_SOURCE_DIR) + "/tests/data/pendulum.urdf";
  rebot::PinocchioDynamics pendulum(path, {"hinge"});
  for (double angle : {-1.2, -0.4, 0.0, 0.8, 1.5}) {
    // COM is one metre along local +X; positive rotation around +Y lowers it.
    const double g = -2.0 * 9.81 * std::cos(angle);
    near(pendulum.gravity({angle})[0], g, 1e-11, "pendulum gravity");
  }
  rebot::PinocchioDynamics weightless(path, {"hinge"}, {0.0, 0.0, 0.0});
  near(weightless.gravity({0.4})[0], 0.0, 1e-12, "zero gravity");
  rebot::PinocchioDynamics inverted(path, {"hinge"}, {0.0, 0.0, 9.81});
  near(inverted.gravity({0.4})[0], -pendulum.gravity({0.4})[0], 1e-12,
       "inverted gravity");
}

void check_dm_model() {
  const std::string path = std::string(REBOT_TEST_SOURCE_DIR) +
                           "/urdf/DM/urdf/ReBot_Arm_DM.urdf";
  const std::vector<std::string> names = {
      "joint1", "joint2", "joint3", "joint4", "joint5", "joint6"};
  const std::vector<double> q = {0.1, -0.8, -0.5, 0.2, 0.15, -0.1};
  rebot::PinocchioDynamics dynamics(path, names);
  const auto g = dynamics.gravity(q);
  // Captured from the source Python Pinocchio 3.9 model with neutral finger joints.
  const std::vector<double> python_g = {
      3.35482618248062e-18, 1.3508952882965248, -7.221099958260734,
      -1.8440316884303196, 0.05076182554779114, 0.00028107511739617675};
  for (std::size_t index = 0; index < q.size(); ++index) {
    near(g[index], python_g[index], 1e-10, "source Python parity");
  }

  // Named mapping must work independently of both URDF order and vector length.
  auto reversed_names = names;
  auto reversed_q = q;
  std::reverse(reversed_names.begin(), reversed_names.end());
  std::reverse(reversed_q.begin(), reversed_q.end());
  rebot::PinocchioDynamics reversed(path, reversed_names);
  const auto reversed_g = reversed.gravity(reversed_q);
  for (std::size_t index = 0; index < q.size(); ++index) {
    near(reversed_g[index], g[q.size() - index - 1], 1e-12, "reordered joint map");
  }
  rebot::PinocchioDynamics subset(path, {"joint3", "joint1"});
  const auto subset_g = subset.gravity({-0.5, 0.1});
  const auto full_g = dynamics.gravity({0.1, 0.0, -0.5, 0.0, 0.0, 0.0});
  near(subset_g[0], full_g[2], 1e-12, "subset joint3");
  near(subset_g[1], full_g[0], 1e-12, "subset joint1");
  const auto& limits = reversed.joint_limits();
  near(limits.effort[0], 7.0, 0.0, "joint6 effort limit");
  near(limits.lower_position[4], -3.14, 0.0, "joint2 lower limit");
  near(limits.upper_position[4], 0.0, 0.0, "joint2 upper limit");

  invalid([&] { dynamics.gravity({0.0}); }, "short configuration");
  auto bad_q = q;
  bad_q[0] = std::numeric_limits<double>::quiet_NaN();
  invalid([&] { dynamics.gravity(bad_q); }, "NaN configuration");
  invalid([&] { rebot::PinocchioDynamics bad(path, {"gripper"}); }, "missing URDF joint");
  invalid([&] { rebot::PinocchioDynamics bad(path, {"joint1", "joint1"}); },
          "duplicate joint");
  invalid([&] { rebot::PinocchioDynamics bad(path, {}); }, "empty controlled set");
}

}  // namespace

int main() {
  try {
    check_analytic_pendulum();
    check_dm_model();
    std::cout << "Dynamics tests passed: analytic pendulum, Python parity, "
                 "named mapping, and input validation\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
