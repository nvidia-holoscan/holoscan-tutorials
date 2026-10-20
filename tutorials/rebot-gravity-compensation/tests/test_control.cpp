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

#include "rebot/control.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void check(bool value) { if (!value) throw std::runtime_error("control check failed"); }
template<class F> void rejects(F&& f) {
  bool rejected = false;
  try { f(); } catch (const std::exception&) { rejected = true; }
  check(rejected);
}
}

int main() {
  rebot::GravityControlSettings settings{{28, 10}, 1.5, 1, .1};
  rebot::GravityController controller(settings);
  const auto initial = controller.compute({.7, -.2}, {2, -.5}, 1, 1000000000, 1000000000);
  check(initial.position == std::vector<double>({.7, -.2}));
  check(initial.kp == std::vector<double>({1.5, 1.5}) && initial.torque[0] == 2);
  check(initial.kd == std::vector<double>({1, 1}) && initial.velocity == std::vector<double>({0, 0}));
  const auto next = controller.compute({.9, -.4}, {2, -.5}, 2, 1002500000, 1002500000);
  check(next.position == std::vector<double>({.9, -.4}));
  check(next.kp == initial.kp && next.kd == initial.kd);
  const auto final = controller.compute({.9, -.4}, {2, -.5}, 3, 1500000000, 1500000000);
  check(final.position == std::vector<double>({.9, -.4}));
  check(final.kp == std::vector<double>({1.5, 1.5}));
  rejects([&] { controller.compute({.9, -.4}, {2, -.5}, 3, 1500000000, 1500000000); });
  rejects([&] { controller.compute({.9, -.4}, {2, -.5}, 4, 1490000000, 1510000000); });
  rejects([&] { controller.compute({.9, -.4}, {2, -.5}, 4, 1500000000, 1700000000); });
  rejects([&] { controller.compute({.9, -.4}, {29, -.5}, 4, 1700000000, 1700000000); });
  rejects([&] { controller.compute({.9}, {2, -.5}, 4, 1700000000, 1700000000); });
  rejects([&] { controller.compute({std::numeric_limits<double>::quiet_NaN(), 0}, {0, 0},
                                  4, 1700000000, 1700000000); });
  controller.reset();
  check(controller.compute({1, 2}, {0, 0}, 1, 1800000000, 1800000000).kp == initial.kp);
  std::cout << "Direct startup and stale/nonfinite/over-limit rejection passed\n";
}
