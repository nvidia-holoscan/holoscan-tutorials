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

#include "rebot/operators/motor_command_sink.hpp"

#include "rebot/control.hpp"
#include "rebot/robot_session.hpp"

#include <stdexcept>
#include <utility>

namespace rebot {

MotorCommandSinkOp::MotorCommandSinkOp(std::shared_ptr<RobotSession> robot)
    : robot_(std::move(robot)) {
  if (!robot_) throw std::invalid_argument("MotorCommandSinkOp requires a robot session");
}

void MotorCommandSinkOp::setup(holoscan::OperatorSpec& spec) {
  spec.input(command, "command").queue_depth(1);
}

holoscan::Contract MotorCommandSinkOp::contract() const {
  holoscan::Contract contract;
  contract.trigger(holoscan::OnEach{command});
  return contract;
}

void MotorCommandSinkOp::start() {
  previous_sequence_ = 0;
}

void MotorCommandSinkOp::stop() { robot_->disable(); }

holoscan::expected<void, holoscan::Error> MotorCommandSinkOp::compute(
    holoscan::ExecutionContext&) {
  auto input = command.receive_data();
  if (!input) return holoscan::make_unexpected(std::move(input).error());
  try {
    const auto& value = *input;
    const auto now = steady_now_ns();
    if (value.sequence <= previous_sequence_ || value.timestamp_ns == 0 ||
        value.timestamp_ns > now ||
        now - value.timestamp_ns > robot_->config.watchdog_ms * 1'000'000ULL) {
      throw std::runtime_error("Expired or out-of-order motor command");
    }

    MitCommand target{value.position, value.velocity, value.kp, value.kd, value.torque};
    {
      std::lock_guard lock(robot_->io_mutex);
      robot_->robot.send_mit(target);
    }

    const auto completed = steady_now_ns();
    previous_sequence_ = value.sequence;
    robot_->last_command_ns.store(completed);
    robot_->commands.fetch_add(1);
    return {};
  } catch (const std::exception& error) {
    robot_->disable();
    return holoscan::make_unexpected(holoscan::Error{holoscan::ErrorCode::kFailure, error.what()});
  }
}

}  // namespace rebot
