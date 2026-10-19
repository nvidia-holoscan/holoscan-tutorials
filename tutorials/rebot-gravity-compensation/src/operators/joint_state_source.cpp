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

#include "rebot/operators/joint_state_source.hpp"

#include "rebot/robot_session.hpp"

#include <chrono>
#include <stdexcept>
#include <utility>

namespace rebot {

JointStateSourceOp::JointStateSourceOp(std::shared_ptr<RobotSession> robot)
    : robot_(std::move(robot)) {
  if (!robot_) throw std::invalid_argument("JointStateSourceOp requires a robot session");
}

void JointStateSourceOp::setup(holoscan::OperatorSpec& spec) {
  spec.output(state, "state");
}

holoscan::Contract JointStateSourceOp::contract() const {
  holoscan::Contract contract;
  contract.trigger(holoscan::OnClock{.period = kControlPeriod});
  return contract;
}

void JointStateSourceOp::start() {
  sequence_ = 0;
}

holoscan::expected<void, holoscan::Error> JointStateSourceOp::compute(
    holoscan::ExecutionContext&) {
  try {
    JointState feedback;
    {
      std::lock_guard lock(robot_->io_mutex);
      feedback = robot_->robot.read_state();
    }
    messages::JointStateT sample;
    sample.sequence = ++sequence_;
    sample.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        feedback.read_at.time_since_epoch()).count();
    sample.position = feedback.position;
    sample.velocity = feedback.velocity;
    sample.torque = feedback.torque;
    return state.emit(sample);
  } catch (const std::exception& error) {
    return holoscan::make_unexpected(holoscan::Error{holoscan::ErrorCode::kFailure, error.what()});
  }
}

}  // namespace rebot
