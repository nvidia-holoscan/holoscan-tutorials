# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""The gravity-compensation demo using HSDK 5.0's native Python operators.

Run from the tutorial directory (tutorials/rebot-gravity-compensation): python python/gravity_compensation.py
Return the arm to its home position manually before pressing Ctrl+C.
"""

import argparse
from pathlib import Path
import signal
import sys
import threading
import time

import holoscan as hsdk
import yaml

from rebot_gravity import GravityCompensation, GravityParams, prepare_gravity_compensation
from rebot_robot import Robot, robot_config


def load_configuration(path):
    text = Path(path).read_text()
    # Validate Holoscan's YAML subset, then decode the application fields.
    hsdk.config.parse(text, hsdk.config.Format.YAML)
    values = yaml.safe_load(text)
    return robot_config(values, path), GravityParams.from_mapping(values.get("gravity_compensation", {}))


def build_graph(robot, controller):
    # A closure keeps these prepared resources shared by all three operators.
    # hsdk.Param values are copied by the SDK; device connections are not parameters.
    class JointStateSourceOp(hsdk.Operator):
        state = hsdk.Output("state", hsdk.pyobject)

        def setup(self, spec):
            spec.output(self.state)

        def contract(self):
            return hsdk.Contract().trigger(hsdk.OnClock(period=hsdk.time.us(2500)))

        def start(self):
            self.sequence = 0

        def compute(self, ctx):
            self.sequence += 1
            self.state.emit(robot.read_state(self.sequence))

    class PinocchioGravityOp(hsdk.Operator):
        state = hsdk.Input("state", hsdk.pyobject)
        command = hsdk.Output("command", hsdk.pyobject)
        kp = hsdk.Param(1.5, range=(0.0, 500.0))
        kd = hsdk.Param(1.0, range=(0.0, 5.0))
        tau_scale = hsdk.Param([1.0] * 6)
        joint_direction = hsdk.Param([1.0] * 6)

        def setup(self, spec):
            spec.input(self.state, queue_depth=1)
            spec.output(self.command)

        def contract(self):
            return hsdk.Contract().trigger(hsdk.OnEach(self.state))

        def compute(self, ctx):
            state = self.state.receive_data()
            command = controller.compute(
                state, kp=self.kp, kd=self.kd,
                tau_scale=self.tau_scale, joint_direction=self.joint_direction)
            self.command.emit(command)

    class MotorCommandSinkOp(hsdk.Operator):
        command = hsdk.Input("command", hsdk.pyobject)

        def setup(self, spec):
            spec.input(self.command, queue_depth=1)

        def contract(self):
            return hsdk.Contract().trigger(hsdk.OnEach(self.command))

        def compute(self, ctx):
            robot.send_command(self.command.receive_data())

        def stop(self):
            robot.disable()

    graph = hsdk.Graph("rebot-gravity-python", default_clock=hsdk.RealtimeClock)
    source = graph.op(JointStateSourceOp, "joint_feedback")
    gravity = graph.op(PinocchioGravityOp, "gravity", **controller.parameters.operator_kwargs())
    sink = graph.op(MotorCommandSinkOp, "motor_commands")
    graph.add_flow(source.state, gravity.state, queue_depth=1, queue_policy=hsdk.QueuePolicy.DROP_OLDEST)
    graph.add_flow(gravity.command, sink.command, queue_depth=1, queue_policy=hsdk.QueuePolicy.DROP_OLDEST)
    graph.partition("control").add(source).add(gravity).add(sink)
    return graph


def run(config_path, cpu=None):
    config, parameters = load_configuration(config_path)
    robot = Robot(config)
    controller = GravityCompensation(config, parameters)
    stop_requested = threading.Event()

    def request_shutdown(signum, frame):
        stop_requested.set()

    previous_handlers = {sig: signal.signal(sig, request_shutdown) for sig in (signal.SIGINT, signal.SIGTERM)}
    try:
        options = hsdk.CompileOptions()
        if cpu is not None:
            if cpu < 0:
                raise ValueError("CPU must be a nonnegative logical CPU number")
            options.deployment.bind_thread_placement(
                "control", hsdk.execution.ThreadPlacement(
                    affinity=hsdk.execution.CPUAffinity([cpu]),
                    probe_failure_policy=hsdk.execution.ProbeFailurePolicy.FAIL_ADMISSION))
        plan = hsdk.compile(build_graph(robot, controller), options)
        if not plan:
            raise RuntimeError(plan.summary())
        if stop_requested.is_set():
            return 0
        print(f"Preparing {config.channel} for gravity compensation at 400 Hz.", flush=True)
        print("Manually return the arm home before Ctrl+C; stopping disables the motors.", flush=True)
        prepare_gravity_compensation(robot, controller)
        if stop_requested.is_set():
            return 0
        announced = False
        with hsdk.run_async(plan) as session:
            while not session.wait(timeout=0.005):
                if stop_requested.is_set():
                    session.request_stop()
                    break
                if time.monotonic_ns() - robot.last_command_ns > config.watchdog_ms * 1_000_000:
                    robot.disable()
                    raise RuntimeError("command completion watchdog expired")
                if robot.commands and not announced:
                    print("Gravity compensation active.", flush=True)
                    announced = True
        if session.termination is not hsdk.RunTermination.STOPPED:
            raise RuntimeError(f"control graph failed: {session.diagnostics}")
        print(f"Stopped after {robot.commands} command batches; motors disabled.", flush=True)
        return 0
    finally:
        robot.close()
        for sig, handler in previous_handlers.items():
            signal.signal(sig, handler)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("config", nargs="?", default="config/rebotarm_dm.yaml", help="robot YAML configuration")
    parser.add_argument("--cpu", type=int, help="pin the control partition to one logical CPU through Holoscan")
    args = parser.parse_args()
    try:
        return run(args.config, args.cpu)
    except Exception as error:
        print(f"gravity_compensation: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
