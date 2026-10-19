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

#include <holoscan/config/codec.hpp>
#include <motor_abi.h>
#include <dlfcn.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <set>
#include <stdexcept>
#include <thread>
#include <utility>

#ifndef REBOT_DEFAULT_MOTOR_LIBRARY
#define REBOT_DEFAULT_MOTOR_LIBRARY ""
#endif

namespace rebot {
namespace {
using Clock = std::chrono::steady_clock;

void require(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

template <typename T>
T value_or(const holoscan::config::ConfigView& object, const char* key, T fallback) {
  const auto value = object.find_member(key);
  return value ? holoscan::config::decode_config<T>(*value) : fallback;
}

template <typename Fn>
Fn symbol(void* library, const char* name) {
  dlerror();
  auto* address = dlsym(library, name);
  const auto* error = dlerror();
  if (error) throw std::runtime_error(std::string("motorbridge symbol ") + name + ": " + error);
  return reinterpret_cast<Fn>(address);
}
}  // namespace

RobotConfig load_robot_config(const holoscan::config::Document& document, const std::string& path) {
  using holoscan::config::ConfigValueKind;
  using holoscan::config::decode_config;
  const auto root = document.root();
  RobotConfig cfg;
  cfg.channel = document.get<std::string>("/channel");
  require(!cfg.channel.empty(), "channel must not be empty");
  require(!root.find_member("rate"), "Control runs at a fixed 400 Hz; remove 'rate' from the configuration");
  cfg.baud = value_or(root, "baud", cfg.baud);
  cfg.watchdog_ms = value_or(root, "watchdog_ms", cfg.watchdog_ms);
  require(cfg.baud > 0, "baud must be positive");
  require(cfg.watchdog_ms >= 20 && cfg.watchdog_ms <= 1000, "watchdog_ms must be in [20,1000]");
  auto urdf = std::filesystem::path(document.get<std::string>("/urdf_path"));
  if (urdf.is_relative()) {
    const auto parent = std::filesystem::absolute(path).parent_path();
    urdf = std::filesystem::exists(parent / urdf) ? parent / urdf : parent.parent_path() / urdf;
  }
  require(std::filesystem::is_regular_file(urdf), "URDF does not exist: " + urdf.string());
  cfg.urdf_path = std::filesystem::weakly_canonical(urdf).string();
  const auto joints = document.at("/joints");
  require(joints.kind() == ConfigValueKind::kArray && joints.size() > 0, "joints must be a nonempty sequence");
  std::set<std::string> names;
  std::set<unsigned> motor_ids, feedback_ids;
  for (std::size_t j = 0; j < joints.size(); ++j) {
    const auto item = joints.element(j);
    JointConfig joint;
    joint.name = decode_config<std::string>(item.member("name"));
    require(!joint.name.empty() && names.insert(joint.name).second, "duplicate or empty joint name: " + joint.name);
    const auto motor_id = decode_config<unsigned>(item.member("motor_id"));
    const auto feedback_id = decode_config<unsigned>(item.member("feedback_id"));
    require(motor_id > 0 && motor_id <= 0xFF && motor_ids.insert(motor_id).second,
            "invalid or duplicate DM motor_id for " + joint.name);
    require(feedback_id <= 0x7FF && feedback_ids.insert(feedback_id).second,
            "invalid or duplicate feedback_id for " + joint.name);
    joint.motor_id = static_cast<std::uint16_t>(motor_id);
    joint.feedback_id = static_cast<std::uint16_t>(feedback_id);
    const auto vendor = value_or(item, "vendor", std::string("damiao"));
    require(vendor == "damiao", "this hardware port supports Damiao motors only: " + joint.name);
    joint.model = decode_config<std::string>(item.member("model"));
    if (joint.model == "4340P" || joint.model == "4340") {
      joint.velocity_limit = 10; joint.torque_limit = 28;
    } else if (joint.model == "4310") {
      joint.velocity_limit = 30; joint.torque_limit = 10;
    } else throw std::runtime_error("unsupported DM model in this port: " + joint.model);
    if (const auto mit = item.find_member("MIT")) {
      joint.kp = value_or(*mit, "kp", 0.0);
      joint.kd = value_or(*mit, "kd", 0.0);
    }
    require(std::isfinite(joint.kp) && joint.kp >= 0 && joint.kp <= 500 &&
            std::isfinite(joint.kd) && joint.kd >= 0 && joint.kd <= 5,
            "MIT gains outside supported range for " + joint.name);
    cfg.joints.push_back(joint);
  }
  std::set<std::size_t> grouped;
  const auto read_group = [&](const char* group, std::vector<std::size_t>& indices) {
    const auto node = document.find(std::string("/groups/") + group);
    if (!node) return;
    for (const auto& name : decode_config<std::vector<std::string>>(node->member("joints"))) {
      auto it = std::find_if(cfg.joints.begin(), cfg.joints.end(), [&](const auto& joint) { return joint.name == name; });
      require(it != cfg.joints.end(), "group refers to unknown joint: " + name);
      const auto index = static_cast<std::size_t>(it - cfg.joints.begin());
      require(grouped.insert(index).second, "joint appears in multiple groups: " + name);
      indices.push_back(index);
    }
  };
  read_group("arm", cfg.arm_indices);
  read_group("gripper", cfg.gripper_indices);
  require(!cfg.arm_indices.empty(), "arm group must not be empty");
  require(grouped.size() == cfg.joints.size(), "every joint must be in arm or gripper group");

  return cfg;
}

void validate_mit_command(const RobotConfig& cfg, const MitCommand& command) {
  const auto n = cfg.joints.size();
  require(command.position.size() == n && command.velocity.size() == n && command.kp.size() == n &&
          command.kd.size() == n && command.torque.size() == n, "MIT command must contain every configured joint");
  for (std::size_t i = 0; i < n; ++i) {
    const auto& joint = cfg.joints[i];
    const auto within = [](double value, double limit) { return std::isfinite(value) && std::abs(value) <= limit; };
    require(within(command.position[i], joint.position_limit) && within(command.velocity[i], joint.velocity_limit) &&
            within(command.torque[i], joint.torque_limit) && within(command.kp[i], 500) && command.kp[i] >= 0 &&
            within(command.kd[i], 5) && command.kd[i] >= 0,
            "MIT command exceeds finite motor limits: " + joint.name);
  }
}

struct Robot::Impl {
  RobotConfig cfg;
  std::string library_path;
  void* library{};
  MotorController* controller{};
  std::vector<MotorHandle*> motors;
  std::vector<bool> owned;
  JointState last_state;
  decltype(&motor_last_error_message) last_error{};
  decltype(&motor_controller_new_dm_serial) new_serial{};
  decltype(&motor_controller_free) free_controller{};
  decltype(&motor_controller_poll_feedback_once) poll{};
  decltype(&motor_controller_close_bus) close_bus{};
  decltype(&motor_controller_add_damiao_motor) add_motor{};
  decltype(&motor_handle_free) free_motor{};
  decltype(&motor_handle_enable) enable_motor{};
  decltype(&motor_handle_disable) disable_motor{};
  decltype(&motor_handle_ensure_mode) ensure_mode{};
  decltype(&motor_handle_request_feedback) request{};
  decltype(&motor_handle_get_state) get_state{};
  decltype(&motor_handle_send_mit) send{};
  decltype(&motor_handle_set_can_timeout_ms) watchdog{};
  decltype(&motor_handle_get_register_u32) get_register{};

  void check(int rc, const std::string& action) {
    if (rc != 0) {
      const auto* message = last_error();
      throw std::runtime_error(action + ": " + (message ? message : "unknown motorbridge error"));
    }
  }

  void send_one(std::size_t i, const MitCommand& command) {
    check(send(motors[i], command.position[i], command.velocity[i], command.kp[i], command.kd[i], command.torque[i]),
          "send MIT " + cfg.joints[i].name);
  }

  void require_connected() { require(controller != nullptr, "robot is not connected"); }
};

Robot::Robot(RobotConfig config, std::string library) : impl_(std::make_unique<Impl>()) {
  impl_->cfg = std::move(config);
  impl_->library_path = std::move(library);
}
Robot::~Robot() { disconnect(); }

void Robot::connect() {
  auto& p = *impl_;
  if (p.controller) return;
  if (p.library_path.empty()) {
    if (const auto* path = std::getenv("MOTORBRIDGE_LIB")) p.library_path = path;
    else if (std::filesystem::exists(REBOT_DEFAULT_MOTOR_LIBRARY)) p.library_path = REBOT_DEFAULT_MOTOR_LIBRARY;
  }
  if (p.library_path.empty()) p.library_path = "libmotor_abi.so";
  p.library = dlopen(p.library_path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!p.library) throw std::runtime_error("load motorbridge " + p.library_path + ": " + dlerror());
  try {
#define REBOT_BIND(member, name) p.member = symbol<decltype(p.member)>(p.library, name)
    REBOT_BIND(last_error, "motor_last_error_message");
    REBOT_BIND(new_serial, "motor_controller_new_dm_serial");
    REBOT_BIND(free_controller, "motor_controller_free");
    REBOT_BIND(poll, "motor_controller_poll_feedback_once");
    REBOT_BIND(close_bus, "motor_controller_close_bus");
    REBOT_BIND(add_motor, "motor_controller_add_damiao_motor");
    REBOT_BIND(free_motor, "motor_handle_free");
    REBOT_BIND(enable_motor, "motor_handle_enable");
    REBOT_BIND(disable_motor, "motor_handle_disable");
    REBOT_BIND(ensure_mode, "motor_handle_ensure_mode");
    REBOT_BIND(request, "motor_handle_request_feedback");
    REBOT_BIND(get_state, "motor_handle_get_state");
    REBOT_BIND(send, "motor_handle_send_mit");
    REBOT_BIND(watchdog, "motor_handle_set_can_timeout_ms");
    REBOT_BIND(get_register, "motor_handle_get_register_u32");
#undef REBOT_BIND
    p.controller = p.new_serial(p.cfg.channel.c_str(), p.cfg.baud);
    p.check(p.controller ? 0 : -1, "connect " + p.cfg.channel);
    for (const auto& joint : p.cfg.joints) {
      auto* motor = p.add_motor(p.controller, joint.motor_id, joint.feedback_id, joint.model.c_str());
      p.check(motor ? 0 : -1, "register " + joint.name);
      p.motors.push_back(motor);
    }
    p.owned.assign(p.motors.size(), false);
  } catch (...) { disconnect(); throw; }
}

JointState Robot::read_state(std::chrono::milliseconds timeout) try {
  auto& p = *impl_;
  p.require_connected();
  require(timeout.count() > 0, "feedback timeout must be positive");
  const bool active = std::any_of(p.owned.begin(), p.owned.end(), [](bool value) { return value; });
  if (active) timeout = std::min(timeout, std::chrono::milliseconds(p.cfg.watchdog_ms));
  const auto deadline = Clock::now() + timeout;
  // Enabled motors reply to MIT commands; disabled motors need a feedback query.
  for (std::size_t i = 0; i < p.motors.size(); ++i)
    if (!p.owned[i]) p.check(p.request(p.motors[i]), "request feedback " + p.cfg.joints[i].name);

  JointState result;
  const auto n = p.motors.size();
  result.position.resize(n); result.velocity.resize(n); result.torque.resize(n); result.status.resize(n);
  for (;;) {
    p.check(p.poll(p.controller), "poll feedback");
    bool ready = true;
    std::string missing;
    // This dates the cache read. The stock C ABI cannot tell us when a motor
    // last replied, so repeated cached values are accepted, even if obsolete.
    result.read_at = Clock::now();
    for (std::size_t i = 0; i < n; ++i) {
      MotorState state{};
      p.check(p.get_state(p.motors[i], &state), "read feedback " + p.cfg.joints[i].name);
      if (!state.has_value) {
        ready = false; missing += " " + p.cfg.joints[i].name; continue;
      }
      require(std::isfinite(state.pos) && std::isfinite(state.vel) && std::isfinite(state.torq),
              "nonfinite feedback: " + p.cfg.joints[i].name);
      require(state.status_code == 0 || state.status_code == 1,
              "DM motor fault " + p.cfg.joints[i].name + " status=" + std::to_string(state.status_code));
      if (p.owned[i] && state.status_code != 1) {
        ready = false; missing += " " + p.cfg.joints[i].name + "(disabled)"; continue;
      }
      result.position[i] = state.pos; result.velocity[i] = state.vel; result.torque[i] = state.torq;
      result.status[i] = state.status_code;
    }
    require(Clock::now() < deadline, "feedback unavailable for:" + missing);
    if (ready) {
      p.last_state = result;
      return result;
    }
    std::this_thread::sleep_for(std::chrono::microseconds(100));
  }
} catch (...) {
  disable();
  throw;
}

void Robot::prepare_mit(const MitCommand& initial) {
  auto& p = *impl_;
  p.require_connected();
  require(std::none_of(p.owned.begin(), p.owned.end(), [](bool value) { return value; }), "motors are already enabled by this instance");
  validate_mit_command(p.cfg, initial);
  // All validation is complete before any mode, watchdog or enable write.
  auto measured = read_state();
  for (std::size_t i = 0; i < p.motors.size(); ++i) {
    require(measured.status[i] == 0, "motor already enabled by another controller: " + p.cfg.joints[i].name);
    require(std::abs(initial.position[i] - measured.position[i]) < 0.15,
            "initial MIT target is not the current measured position: " + p.cfg.joints[i].name);
  }
  try {
    for (std::size_t i = 0; i < p.motors.size(); ++i) {
      p.check(p.ensure_mode(p.motors[i], 1, 1000), "ensure MIT " + p.cfg.joints[i].name);
      p.check(p.watchdog(p.motors[i], p.cfg.watchdog_ms), "set CAN watchdog " + p.cfg.joints[i].name);
      std::uint32_t watchdog_value{};
      p.check(p.get_register(p.motors[i], 9, 100, &watchdog_value), "verify CAN watchdog " + p.cfg.joints[i].name);
      require(watchdog_value == p.cfg.watchdog_ms * 20, "CAN watchdog verification failed for " + p.cfg.joints[i].name);
    }
    // Mode changes can take seconds. Re-read the cache before enabling.
    // This can reject observed movement but cannot verify receipt of new feedback.
    measured = read_state();
    for (std::size_t i = 0; i < p.motors.size(); ++i)
      require(std::abs(initial.position[i] - measured.position[i]) < 0.15,
              "robot moved during MIT setup; rerun from its current pose");
    for (std::size_t i = 0; i < p.motors.size(); ++i) {
      p.send_one(i, initial);
      // Own before sending enable: a partial/failed write must still be disabled.
      p.owned[i] = true;
      p.check(p.enable_motor(p.motors[i]), "enable " + p.cfg.joints[i].name);
      p.send_one(i, initial);
    }
  } catch (...) { disable(); throw; }
}

void Robot::send_mit(const MitCommand& command) {
  auto& p = *impl_;
  p.require_connected();
  try {
    validate_mit_command(p.cfg, command);
    require(p.last_state.read_at != Clock::time_point{} &&
            Clock::now() - p.last_state.read_at < std::chrono::milliseconds(p.cfg.watchdog_ms),
            "joint state has not been read recently");
    require(std::any_of(p.owned.begin(), p.owned.end(), [](bool value) { return value; }), "no motors enabled by this instance");
    for (std::size_t i = 0; i < p.motors.size(); ++i) if (p.owned[i]) p.send_one(i, command);
  } catch (...) { disable(); throw; }
}

void Robot::disable() noexcept {
  auto& p = *impl_;
  for (std::size_t i = 0; i < p.owned.size(); ++i) {
    if (p.owned[i] && p.disable_motor) {
      // Retry transient serial errors. The firmware watchdog remains the fallback.
      for (int attempt = 0; attempt < 3; ++attempt) if (p.disable_motor(p.motors[i]) == 0) break;
      p.owned[i] = false;
    }
  }
}

void Robot::disconnect() noexcept {
  auto& p = *impl_;
  disable();
  if (p.controller && p.close_bus) p.close_bus(p.controller);  // shutdown() would disable unowned motors.
  for (auto* motor : p.motors) if (p.free_motor) p.free_motor(motor);
  p.motors.clear(); p.owned.clear();
  if (p.controller && p.free_controller) p.free_controller(p.controller);
  p.controller = nullptr;
  if (p.library) dlclose(p.library);
  p.library = nullptr;
}
}  // namespace rebot
