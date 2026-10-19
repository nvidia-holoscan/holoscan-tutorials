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

#pragma once

#include <cstdint>
#include <vector>

namespace rebot {

// Values are in motor order; positions rad, velocities rad/s and torques N*m.
struct GravityControlSettings {
  std::vector<double> torque_limit;
  double kp = 1.5;
  double kd = 1.0;
  double max_state_age_seconds = 0.1;
};

struct GravityCommand {
  std::uint64_t sequence = 0;
  std::uint64_t timestamp_ns = 0;
  std::vector<double> position, velocity, kp, kd, torque;
};

// Combines gravity feedforward with measured position targets and fixed gains.
// A decreasing timestamp, expired state or invalid torque rejects the command.
class GravityController {
 public:
  explicit GravityController(GravityControlSettings settings);
  GravityCommand compute(const std::vector<double>& position,
                         const std::vector<double>& gravity_torque,
                         std::uint64_t sequence, std::uint64_t state_timestamp_ns,
                         std::uint64_t now_ns);
  void reset();

 private:
  GravityControlSettings settings_;
  std::uint64_t previous_sequence_ = 0;
  std::uint64_t previous_state_ns_ = 0;
  bool initialized_ = false;
};

std::uint64_t steady_now_ns();
}  // namespace rebot
