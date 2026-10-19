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

#include "rebot/gravity_compensation.hpp"

#include "rebot/robot_session.hpp"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <utility>

namespace rebot {

GravityCompensation::GravityCompensation(RobotConfig config) : config_(std::move(config)) {}

void GravityCompensation::initialize(const GravityParams& parameters) {
  if (dynamics_) throw std::logic_error("Gravity compensation is already initialized");
  if (config_.arm_indices.size() != parameters.tau_scale.size()) {
    throw std::invalid_argument("Gravity compensation requires six arm joints");
  }
  std::vector<std::string> names;
  for (const auto index : config_.arm_indices) names.push_back(config_.joints.at(index).name);
  dynamics_ = std::make_unique<PinocchioDynamics>(config_.urdf_path, std::move(names));

  GravityControlSettings settings;
  settings.kp = parameters.kp;
  settings.kd = parameters.kd;
  settings.max_state_age_seconds = config_.watchdog_ms * 0.001;
  for (std::size_t j = 0; j < config_.arm_indices.size(); ++j) {
    const auto i = config_.arm_indices[j];
    settings.torque_limit.push_back(std::min(config_.joints[i].torque_limit,
                                           dynamics_->joint_limits().effort.at(j)));
  }
  controller_ = std::make_unique<GravityController>(std::move(settings));
}

messages::GravityCommandT GravityCompensation::compute(const messages::JointStateT& sample,
    const GravityParams& parameters, std::uint64_t now_ns) {
  if (!controller_) throw std::logic_error("Gravity compensation has not been initialized");
  if (sample.position.size() != config_.joints.size()) {
    throw std::invalid_argument("Joint state size does not match robot configuration");
  }
  std::vector<double> q;
  for (const auto index : config_.arm_indices) q.push_back(sample.position[index]);
  auto model_q = q;
  for (std::size_t i = 0; i < q.size(); ++i) model_q[i] *= parameters.joint_direction.at(i);
  const auto& limits = dynamics_->joint_limits();
  for (std::size_t i = 0; i < q.size(); ++i) {
    // Small allowance for encoder quantization and mechanical zero calibration.
    constexpr double tolerance = .035;
    if (model_q[i] < limits.lower_position[i] - tolerance ||
        model_q[i] > limits.upper_position[i] + tolerance) {
      throw std::runtime_error("Joint position outside URDF limits: " +
                               config_.joints[config_.arm_indices[i]].name);
    }
  }
  auto gravity = dynamics_->gravity(model_q);
  for (std::size_t i = 0; i < q.size(); ++i) {
    gravity[i] *= parameters.joint_direction[i] * parameters.tau_scale.at(i);
  }
  const auto arm = controller_->compute(q, gravity, sample.sequence, sample.timestamp_ns, now_ns);
  messages::GravityCommandT output;
  output.sequence = arm.sequence;
  output.timestamp_ns = arm.timestamp_ns;
  output.position = sample.position;
  const auto n = config_.joints.size();
  output.velocity.assign(n, 0);
  output.torque.assign(n, 0);
  for (const auto& joint : config_.joints) {
    output.kp.push_back(joint.kp);
    output.kd.push_back(joint.kd);
  }
  for (std::size_t j = 0; j < config_.arm_indices.size(); ++j) {
    const auto i = config_.arm_indices[j];
    output.position[i] = arm.position[j];
    output.kp[i] = arm.kp[j];
    output.kd[i] = arm.kd[j];
    output.torque[i] = arm.torque[j];
  }
  return output;
}

void prepare_gravity_compensation(RobotSession& session, GravityCompensation& gravity,
                                  const GravityParams& parameters) {
  // Model and controller validation happen before opening the motor connection.
  gravity.initialize(parameters);
  std::lock_guard lock(session.io_mutex);
  session.robot.connect();
  const auto feedback = session.robot.read_state();
  messages::JointStateT initial;
  initial.sequence = 0;  // The graph starts with sample 1.
  initial.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
      feedback.read_at.time_since_epoch()).count();
  initial.position = feedback.position;
  initial.velocity = feedback.velocity;
  initial.torque = feedback.torque;
  const auto command = gravity.compute(initial, parameters, steady_now_ns());
  session.robot.prepare_mit(
      {command.position, command.velocity, command.kp, command.kd, command.torque});
  // Supervise command progress starting with preparation, including the first tick.
  session.last_command_ns.store(steady_now_ns());
}

}  // namespace rebot
