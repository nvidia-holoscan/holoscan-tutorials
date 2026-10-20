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

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace rebot {

struct JointLimits {
  std::vector<double> lower_position;
  std::vector<double> upper_position;
  std::vector<double> velocity;
  std::vector<double> effort;
};

/// Reusable fixed-base Pinocchio model, independent of Holoscan and the motor driver.
///
/// Inputs and outputs use the supplied joint-name order, in radians (revolute),
/// metres (prismatic), and N*m or N. Unselected model joints remain at their
/// neutral configuration with zero velocity/acceleration. In particular, the
/// DM finger joints are held neutral when computing dynamics for the arm.
/// Each instance owns reusable Pinocchio workspaces and must be used serially.
class PinocchioDynamics {
 public:
  PinocchioDynamics(const std::string& urdf_path,
                    std::vector<std::string> joint_names,
                    std::array<double, 3> gravity = {0.0, 0.0, -9.81});
  ~PinocchioDynamics();
  PinocchioDynamics(PinocchioDynamics&&) noexcept;
  PinocchioDynamics& operator=(PinocchioDynamics&&) noexcept;
  PinocchioDynamics(const PinocchioDynamics&) = delete;
  PinocchioDynamics& operator=(const PinocchioDynamics&) = delete;

  const JointLimits& joint_limits() const;

  /// Torque that balances gravity: g(q), equal to RNEA(q, 0, 0).
  std::vector<double> gravity(const std::vector<double>& position);


 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace rebot
