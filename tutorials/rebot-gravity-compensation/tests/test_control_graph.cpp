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

#include "fake_motor.hpp"
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

#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>

namespace {
using namespace std::chrono_literals;
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }

holoscan::ExecutionPlan make_plan(const std::shared_ptr<rebot::RobotSession>& robot,
                                 const std::shared_ptr<rebot::GravityCompensation>& controller,
                                 const holoscan::config::Document& config) {
  holoscan::Graph graph{"gravity_test"};
  const auto source = graph.op<rebot::JointStateSourceOp>("feedback", robot);
  auto gravity = graph.op<rebot::PinocchioGravityOp>("gravity", controller);
  gravity.set_params(holoscan::params::decode<rebot::GravityParams>(config, "/gravity_compensation"));
  const auto sink = graph.op<rebot::MotorCommandSinkOp>("commands", robot);
  const holoscan::ConnectionOptions edge{
      .queue_policy = holoscan::QueuePolicy::kDropOldest, .queue_depth = 1};
  graph.add_flow(source->state, gravity->state, edge);
  graph.add_flow(gravity->command, sink->command, edge);
  graph.set_default_clock(graph.add_clock<holoscan::RealtimeClock>("clock"));
  graph.partition("control").include_all();
  return holoscan::compile(graph);
}
}

int main() try {
  FakeMotor fake;
  const auto path = std::string(REBOT_TEST_SOURCE_DIR) + "/config/rebotarm_dm.yaml";
  using holoscan::config::Edit;
  using holoscan::config::encode_config;
  const auto document = holoscan::config_io::load(path, holoscan::config_io::Format::kYaml)
      .patched(Edit::replace("/gravity_compensation/kp", encode_config(2.25)));
  const auto config = rebot::load_robot_config(document, path);
  const auto parameters = holoscan::params::decode<rebot::GravityParams>(document, "/gravity_compensation");
  for (bool fail : {false, true}) {
    fake.reset();
    auto robot = std::make_shared<rebot::RobotSession>(config, REBOT_TEST_MOTOR_LIBRARY);
    auto controller = std::make_shared<rebot::GravityCompensation>(config);
    // Runtime constructors must retain the shared session after Graph is gone.
    const auto plan = make_plan(robot, controller, document);
    check(plan.ok(), "gravity graph compilation failed");
    check(fake.writes() == 0 && fake.enables() == 0, "graph compilation must not configure motors");
    rebot::prepare_gravity_compensation(*robot, *controller, *parameters);
    check(fake.enables() == 7 && fake.mit_writes() > 0,
          "motors and initial command must be prepared before graph execution");
    check(fake.first_kp() == 2.25F, "initial command must use the configured compliant gain");
    check(robot->commands.load() == 0 && robot->last_command_ns.load() != 0,
          "preparation must start the watchdog before the first graph command");
    auto run = holoscan::run_async(plan);
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (run.is_running() && robot->commands.load() < 30 && std::chrono::steady_clock::now() < deadline)
      std::this_thread::sleep_for(2ms);
    const bool progressed = robot->commands.load() >= 30;
    if (fail && progressed) {
      fake.fail_send(1);
      while (run.is_running() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(2ms);
    }
    run.request_stop();
    run.wait();
    for (const auto& diagnostic : run.diagnostics())
      std::cerr << holoscan::format_diagnostic(diagnostic, plan.graph()) << '\n';
    check(progressed, "gravity graph did not complete commands");
    check(fake.first_kp() == 2.25F, "configured parameters did not reach motor commands");
    check((run.termination() == holoscan::RunTermination::kFailed) == fail,
          "graph termination did not match injected transport failure");
    check(fake.enables() == 7 && fake.disables() == 7, "graph must disable all owned motors on stop or error");
  }
  fake.reset();
  auto invalid_config = config;
  invalid_config.urdf_path = "/nonexistent/rebot-test-model.urdf";
  rebot::RobotSession robot(invalid_config, REBOT_TEST_MOTOR_LIBRARY);
  rebot::GravityCompensation controller(invalid_config);
  bool rejected = false;
  try { rebot::prepare_gravity_compensation(robot, controller, *parameters); }
  catch (const std::exception&) { rejected = true; }
  check(rejected && fake.enables() == 0 && fake.writes() == 0,
        "an invalid model must fail preparation before motor writes");
  std::cout << "Explicit preparation, shared lifetime, graph progress, and stop/error cleanup passed\n";
} catch (const std::exception& error) {
  std::cerr << error.what() << '\n'; return 1;
}
