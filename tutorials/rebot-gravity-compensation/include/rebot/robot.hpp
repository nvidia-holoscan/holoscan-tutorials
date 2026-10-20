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

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace holoscan::config { class Document; }

namespace rebot {

inline constexpr std::chrono::nanoseconds kControlPeriod{2'500'000};
inline constexpr double kControlRateHz = 1e9 / kControlPeriod.count();

struct JointConfig {
  std::string name;
  std::uint16_t motor_id{}, feedback_id{};
  std::string model;
  double kp{}, kd{};
  double position_limit{12.5}, velocity_limit{}, torque_limit{};
};

struct RobotConfig {
  std::string channel, urdf_path;
  std::uint32_t baud{921600}, watchdog_ms{100};
  std::vector<JointConfig> joints;
  std::vector<std::size_t> arm_indices, gripper_indices;
};

RobotConfig load_robot_config(const holoscan::config::Document& document, const std::string& path);

// All vectors follow RobotConfig::joints order. Units: rad, rad/s, N*m.
struct JointState {
  std::vector<double> position, velocity, torque;
  std::vector<std::uint8_t> status;
  // Host cache-read time. Stock motorbridge exposes no sensor timestamp or age.
  std::chrono::steady_clock::time_point read_at{};
};

struct MitCommand {
  std::vector<double> position, velocity, kp, kd, torque;
};

// Checks the complete batch before any actuator command is transmitted.
void validate_mit_command(const RobotConfig& config, const MitCommand& command);

// The C++ owner of the same motorbridge transport used by the Python project.
// connect/read_state are read-only at the motor (feedback requests are sent).
// Destruction disables only motors enabled by this instance, then closes the bus.
class Robot {
 public:
  explicit Robot(RobotConfig config, std::string library = "");
  ~Robot();
  Robot(const Robot&) = delete;
  Robot& operator=(const Robot&) = delete;

  void connect();
  JointState read_state(std::chrono::milliseconds timeout = std::chrono::milliseconds(250));
  void prepare_mit(const MitCommand& initial);
  void send_mit(const MitCommand& command);
  void disable() noexcept;

 private:
  void disconnect() noexcept;
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace rebot
