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
#include "rebot/operators/joint_state_source.hpp"
#include "rebot/operators/motor_command_sink.hpp"
#include "rebot/operators/pinocchio_gravity.hpp"
#include "rebot/robot_session.hpp"

#include <holoscan/core/compile.hpp>
#include <holoscan/core/graph.hpp>
#include <holoscan/core/run.hpp>
#include <holoscan/config/io.hpp>
#include <holoscan/params/config.hpp>
#include <holoscan/time/realtime_clock.hpp>

#include <atomic>
#include <charconv>
#include <chrono>
#include <csignal>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <thread>

namespace {
std::atomic<bool> stop_requested{false};
static_assert(std::atomic<bool>::is_always_lock_free);

void request_shutdown(int) { stop_requested.store(true, std::memory_order_relaxed); }
}  // namespace

int main(int argc, char** argv) {
  const auto usage = [&] {
    std::cout << "Usage: " << argv[0] << " [--cpu CPU] [config/rebotarm_dm.yaml]\n"
              << "Runs gravity compensation on the physical arm until Ctrl+C.\n"
              << "--cpu pins the control partition to one logical CPU through Holoscan.\n"
              << "Return the arm home manually before stopping; stopping disables the motors.\n";
  };
  const char* config_path = "config/rebotarm_dm.yaml";
  bool has_config = false;
  std::optional<std::uint32_t> control_cpu;
  for (int i = 1; i < argc; ++i) {
    const std::string_view argument{argv[i]};
    if (argument == "--help") {
      usage();
      return 0;
    }
    if (argument == "--cpu" && !control_cpu && i + 1 < argc) {
      const std::string_view value{argv[++i]};
      std::uint32_t cpu{};
      const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), cpu);
      if (error != std::errc{} || end != value.data() + value.size()) {
        usage();
        return 1;
      }
      control_cpu = cpu;
    } else if (argument.starts_with("-") || has_config) {
      usage();
      return 1;
    } else {
      config_path = argv[i];
      has_config = true;
    }
  }

  try {
    // Nsight may create threads before main(), so use caller-managed signals.
    // The handlers only set a lock-free flag; the main loop stops the graph.
    if (std::signal(SIGINT, request_shutdown) == SIG_ERR ||
        std::signal(SIGTERM, request_shutdown) == SIG_ERR) {
      throw std::runtime_error("Could not install shutdown signal handlers");
    }

    const auto config = holoscan::config_io::load(config_path, holoscan::config_io::Format::kYaml);
    auto robot = std::make_shared<rebot::RobotSession>(rebot::load_robot_config(config, config_path));
    auto controller = std::make_shared<rebot::GravityCompensation>(robot->config);
    const auto parameters = holoscan::params::decode<rebot::GravityParams>(config, "/gravity_compensation");

    // Describe the hardware loop. Constructors only retain configuration;
    // model loading and motor preparation happen after graph compilation.
    holoscan::Graph graph{"gravity_compensation"};
    const auto source = graph.op<rebot::JointStateSourceOp>("joint_feedback", robot);
    auto gravity = graph.op<rebot::PinocchioGravityOp>("gravity", controller);
    gravity.set_params(parameters);
    const auto sink = graph.op<rebot::MotorCommandSinkOp>("motor_commands", robot);

    const holoscan::ConnectionOptions edge{
        .queue_policy = holoscan::QueuePolicy::kDropOldest, .queue_depth = 1};
    graph.add_flow(source->state, gravity->state, edge);
    graph.add_flow(gravity->command, sink->command, edge);
    graph.set_default_clock(graph.add_clock<holoscan::RealtimeClock>("control_clock"));
    graph.partition("control").include_all();

    // Validate the graph before opening the device or enabling a motor.
    holoscan::CompileOptions options;
    if (control_cpu) {
      options.deployment.bind_thread_placement(
          "control", holoscan::ThreadPlacement{
                         .affinity = holoscan::CPUAffinity{*control_cpu},
                         .probe_failure_policy = holoscan::ProbeFailurePolicy::kFailAdmission});
    }
    const auto plan = holoscan::compile(graph, options);
    if (!plan.ok()) {
      for (const auto& diagnostic : plan.diagnostics()) {
        std::cerr << holoscan::format_diagnostic(diagnostic, plan.graph()) << '\n';
      }
      return 1;
    }

    if (stop_requested.load(std::memory_order_relaxed)) return 0;
    std::cout << "Starting gravity compensation at 400 Hz. Return the arm home manually before stopping.\n"
              << std::flush;
    rebot::prepare_gravity_compensation(*robot, *controller, *parameters);
    if (stop_requested.load(std::memory_order_relaxed)) {
      robot->disable();
      return 0;
    }
    auto run = holoscan::run_async(plan);
    bool watchdog_tripped = false;
    bool announced = false;
    while (run.is_running()) {
      if (stop_requested.load(std::memory_order_relaxed)) {
        run.request_stop();
        break;
      }
      const auto last_command = robot->last_command_ns.load();
      if (last_command != 0) {
        if (!announced && robot->commands.load() != 0) {
          std::cout << "Gravity compensation active. Press Ctrl+C to stop.\n" << std::flush;
          announced = true;
        }
        if (rebot::steady_now_ns() - last_command > robot->config.watchdog_ms * 1'000'000ULL) {
          std::cerr << "Control watchdog: motor commands stopped completing.\n";
          watchdog_tripped = true;
          run.request_stop();
          break;
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    run.wait();
    robot->disable();
    for (const auto& diagnostic : run.diagnostics()) {
      std::cerr << holoscan::format_diagnostic(diagnostic, plan.graph()) << '\n';
    }
    std::cout << "Stopped after " << robot->commands.load() << " commands.\n";
    return watchdog_tripped || run.termination() == holoscan::RunTermination::kFailed ? 1 : 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
