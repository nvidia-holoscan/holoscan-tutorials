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

#include "rebot/robot.hpp"
#include "rebot/gravity_compensation.hpp"
#include "fake_motor.hpp"

#include <holoscan/config/io.hpp>
#include <holoscan/params/config.hpp>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template <typename F> void rejects(F action, const char* message) {
  try { action(); } catch (const std::exception&) { return; }
  throw std::runtime_error(message);
}
rebot::MitCommand command(const rebot::RobotConfig& cfg) {
  rebot::MitCommand result;
  for (const auto& joint : cfg.joints) {
    result.position.push_back(0); result.velocity.push_back(0); result.torque.push_back(0);
    result.kp.push_back(joint.kp); result.kd.push_back(joint.kd);
  }
  return result;
}
}

int main() try {
  const auto source = std::filesystem::path(REBOT_TEST_SOURCE_DIR);
  const auto path = (source / "config/rebotarm_dm.yaml").string();
  const auto document = holoscan::config_io::load(path, holoscan::config_io::Format::kYaml);
  const auto cfg = rebot::load_robot_config(document, path);
  const auto gravity = holoscan::params::decode<rebot::GravityParams>(document, "/gravity_compensation");
  check(cfg.joints.size() == 7 && cfg.arm_indices.size() == 6 && cfg.gripper_indices.size() == 1,
        "hardware joint groups");
  check(cfg.joints[0].motor_id == 1 && cfg.joints[0].feedback_id == 0x11 && cfg.joints[6].name == "gripper",
        "motor ID decoding and configured order");
  check(cfg.joints[0].torque_limit == 28 && cfg.joints[3].torque_limit == 10, "DM torque ranges");
  check(cfg.joints[6].kd == 1 && gravity->kp == 1.5 && gravity->kd == 1 && gravity->joint_direction[5] == 1,
        "gripper gains, arm gains, and direction");
  check(std::filesystem::is_regular_file(cfg.urdf_path), "relative URDF resolution");
  auto cmd = command(cfg);
  rebot::validate_mit_command(cfg, cmd);
  cmd.torque[2] = 29;
  rejects([&] { rebot::validate_mit_command(cfg, cmd); }, "accepts excess torque");
  cmd.torque[2] = std::numeric_limits<double>::quiet_NaN();
  rejects([&] { rebot::validate_mit_command(cfg, cmd); }, "accepts NaN torque");
  cmd = command(cfg); cmd.position.pop_back();
  rejects([&] { rebot::validate_mit_command(cfg, cmd); }, "accepts truncated batch");
  cmd = command(cfg); cmd.kd[0] = 8;
  rejects([&] { rebot::validate_mit_command(cfg, cmd); }, "silently clamps damping");

  using holoscan::config::Edit;
  using holoscan::config::encode_config;
  const auto duplicate = document.patched(Edit::replace("/joints/1/motor_id", encode_config(1)));
  rejects([&] { rebot::load_robot_config(duplicate, path); }, "accepts duplicate motor IDs");
  rejects([&] {
    (void)holoscan::config_io::parse("kp: 1\nkp: 2\n", holoscan::config_io::Format::kYaml);
  }, "accepts duplicate YAML keys");
  for (const auto* text : {
      "gravity_compensation: {kd: 8.0}",
      "gravity_compensation: {kp: '1.5'}",
      "gravity_compensation: {transition_duration: 0.0}",
      "gravity_compensation: {tau_scale: [1.0]}",
      "gravity_compensation: {joint_direction: [1, 1, 1, 1, 1, 0]}",
      "gravity_compensation: {kpp: 1.5}"}) {
    rejects([&] {
      const auto invalid = holoscan::config_io::parse(text, holoscan::config_io::Format::kYaml);
      (void)holoscan::params::decode<rebot::GravityParams>(invalid, "/gravity_compensation");
    }, "accepts invalid gravity parameters");
  }

  FakeMotor fake;
  fake.reset();
  {
    rebot::Robot robot(cfg, REBOT_TEST_MOTOR_LIBRARY);
    robot.connect();
    const auto state = robot.read_state();
    fake.freeze(1);
    const auto cached = robot.read_state();
    check(cached.position == state.position && cached.read_at >= state.read_at,
          "stock ABI must accept repeated cache values without claiming sensor freshness");
  }
  check(fake.writes() == 0, "feedback reads must not change motor settings");
  fake.reset(); fake.freeze(1);
  {
    rebot::Robot robot(cfg, REBOT_TEST_MOTOR_LIBRARY);
    robot.connect();
    rejects([&] { robot.read_state(std::chrono::milliseconds(2)); }, "accepts missing initial feedback");
  }
  check(fake.writes() == 0, "missing initial feedback must not enable motors");

  for (int status : {1, 13}) {
    fake.reset();
    {
      rebot::Robot robot(cfg, REBOT_TEST_MOTOR_LIBRARY);
      robot.connect(); robot.read_state(); fake.set_status(3, status);
      rejects([&] { robot.prepare_mit(command(cfg)); }, "accepts externally enabled or faulted motor");
    }
    check(fake.writes() == 0, "must not enable, clear faults, or disable unowned motors");
  }
  fake.reset();
  {
    rebot::Robot robot(cfg, REBOT_TEST_MOTOR_LIBRARY);
    robot.connect(); cmd = command(cfg); cmd.position[3] = .2;
    rejects([&] { robot.prepare_mit(cmd); }, "accepts startup position mismatch");
  }
  check(fake.writes() == 0, "position mismatch must be rejected before control writes");

  for (bool transport_error : {false, true}) {
    fake.reset();
    {
      rebot::Robot robot(cfg, REBOT_TEST_MOTOR_LIBRARY);
      robot.connect(); robot.prepare_mit(command(cfg)); robot.read_state();
      check(fake.enables() == 7, "startup must enable configured motors");
      robot.send_mit(command(cfg));
      const auto before = fake.mit_writes();
      cmd = command(cfg);
      if (transport_error) fake.fail_send(1);
      else cmd.torque[6] = 100;
      rejects([&] { robot.send_mit(cmd); }, "accepts failed or invalid command");
      check(fake.mit_writes() == before + (transport_error ? 1 : 0), "batch validation precedes motor writes");
      check(fake.disables() == 7, "error must disable all owned motors");
    }
    check(fake.disables() == 7, "cleanup must not disable twice");
  }
  fake.reset();
  {
    rebot::Robot robot(cfg, REBOT_TEST_MOTOR_LIBRARY);
    robot.connect(); robot.prepare_mit(command(cfg)); robot.read_state();
    fake.freeze(1);
    robot.read_state(); robot.send_mit(command(cfg));
    check(fake.disables() == 0, "stock ABI cannot detect silence after valid cached feedback");
  }
  check(fake.disables() == 7, "destruction must disable owned motors");
  std::cout << "Driver limits, ownership, failures, and stock cache limitation passed\n";
} catch (const std::exception& error) {
  std::cerr << error.what() << '\n'; return 1;
}
