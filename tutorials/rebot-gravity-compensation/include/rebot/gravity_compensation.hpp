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

#include "rebot/control.hpp"
#include "rebot/dynamics.hpp"
#include "rebot/messages.hpp"
#include "rebot/robot.hpp"

#include <holoscan/core/parameter_schema.hpp>

#include <array>
#include <cmath>
#include <memory>

namespace rebot {

struct RobotSession;

// Six arm joints receive gravity compensation; the gripper keeps its MIT gains.
struct GravityParams {
  double kp{1.5}, kd{1.0};
  std::array<double, 6> tau_scale{1, 1, 1, 1, 1, 1};
  std::array<double, 6> joint_direction{1, 1, 1, 1, 1, 1};
};

inline bool finite_scales(const std::array<double, 6>& values) noexcept {
  for (const auto value : values) if (!std::isfinite(value)) return false;
  return true;
}
inline bool unit_directions(const std::array<double, 6>& values) noexcept {
  for (const auto value : values) if (value != 1 && value != -1) return false;
  return true;
}

HOLOSCAN_PARAM_SCHEMA(
    GravityParams, holoscan::param_schema("rebot.GravityParams", 1),
    holoscan::field<&GravityParams::kp>("kp").range(0.0, 500.0),
    holoscan::field<&GravityParams::kd>("kd").range(0.0, 5.0),
    holoscan::field<&GravityParams::tau_scale>("tau_scale")
        .validator<finite_scales>("rebot.finite_scales.v1"),
    holoscan::field<&GravityParams::joint_direction>("joint_direction")
        .validator<unit_directions>("rebot.unit_directions.v1"));

// Owns the model and controller for one run. Preparation and the gravity
// operator use the same calculation; compute() is called serially.
class GravityCompensation {
 public:
  explicit GravityCompensation(RobotConfig config);
  void initialize(const GravityParams& parameters);
  messages::GravityCommandT compute(const messages::JointStateT& state,
                                   const GravityParams& parameters,
                                   std::uint64_t now_ns);

 private:
  RobotConfig config_;
  std::unique_ptr<PinocchioDynamics> dynamics_;
  std::unique_ptr<GravityController> controller_;
};

// Call after graph compilation, immediately before execution. Loads the model,
// reads the initial pose, and enables the motors with gravity torque and final gains.
void prepare_gravity_compensation(RobotSession& robot, GravityCompensation& gravity,
                                  const GravityParams& parameters);

}  // namespace rebot
