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

#include <chrono>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

namespace rebot {
namespace {
void require(bool predicate, const char* message) {
  if (!predicate) throw std::invalid_argument(message);
}
bool nonnegative(double value) { return std::isfinite(value) && value >= 0; }
}

std::uint64_t steady_now_ns() {
  return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());
}

GravityController::GravityController(GravityControlSettings settings)
    : settings_(std::move(settings)) {
  require(!settings_.torque_limit.empty(), "Torque limits must be nonempty");
  require(nonnegative(settings_.kp) && nonnegative(settings_.kd) &&
              std::isfinite(settings_.max_state_age_seconds) &&
              settings_.max_state_age_seconds > 0,
          "Invalid gravity control settings");
  for (const auto limit : settings_.torque_limit) {
    require(std::isfinite(limit) && limit > 0, "Invalid joint torque limit");
  }
}

void GravityController::reset() {
  initialized_ = false;
  previous_sequence_ = previous_state_ns_ = 0;
}

GravityCommand GravityController::compute(const std::vector<double>& position,
    const std::vector<double>& gravity_torque, std::uint64_t sequence,
    std::uint64_t state_timestamp_ns, std::uint64_t now_ns) {
  const auto n = settings_.torque_limit.size();
  require(position.size() == n && gravity_torque.size() == n, "Gravity command dimension mismatch");
  require(state_timestamp_ns > 0 && state_timestamp_ns <= now_ns,
          "Invalid state timestamp");
  require((now_ns - state_timestamp_ns) * 1e-9 <= settings_.max_state_age_seconds,
          "Expired joint state");
  require(!initialized_ || (sequence > previous_sequence_ &&
                           state_timestamp_ns >= previous_state_ns_),
          "Out-of-order control sample");
  for (std::size_t i = 0; i < n; ++i) {
    require(std::isfinite(position[i]) && std::isfinite(gravity_torque[i]),
            "Nonfinite joint position or gravity torque");
    if (std::abs(gravity_torque[i]) > settings_.torque_limit[i]) {
      throw std::runtime_error("Gravity torque exceeds limit for joint " + std::to_string(i + 1));
    }
  }
  initialized_ = true;
  previous_sequence_ = sequence;
  previous_state_ns_ = state_timestamp_ns;
  GravityCommand command;
  command.sequence = sequence;
  command.timestamp_ns = state_timestamp_ns;
  command.position = position;
  command.velocity.assign(n, 0.0);
  command.kp.assign(n, settings_.kp);
  command.kd.assign(n, settings_.kd);
  command.torque = gravity_torque;
  return command;
}
}  // namespace rebot
