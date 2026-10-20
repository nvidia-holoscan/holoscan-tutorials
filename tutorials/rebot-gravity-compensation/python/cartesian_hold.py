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

"""Hold a Cartesian point with five HSDK 5.0 operators.

Start at home, hand-position in gravity compensation, then enter h to hold.
Enter g to release the hold. Return home manually before Ctrl+C.
"""

import argparse
import os
from pathlib import Path
import select
import signal
import sys
import threading
import time

import holoscan as hsdk
import yaml

import cartesian_operators as ops
from gravity_compensation import load_configuration as load_robot_configuration
from rebot_cartesian import (CartesianMotorCommand, CartesianParams, HoldTarget,
                             ToolKinematics, prepare_cartesian_hold)
from rebot_gravity import GravityCompensation
from rebot_robot import Robot, require


def load_configuration(path):
    text = Path(path).read_text()
    hsdk.config.parse(text, hsdk.config.Format.YAML)
    values = yaml.safe_load(text)
    require(isinstance(values, dict), "Cartesian configuration must be a mapping")
    robot_path = values.get("robot_config")
    require(isinstance(robot_path, str) and robot_path, "robot_config must name a robot YAML file")
    robot_path = Path(path).resolve().parent / robot_path
    config, gravity_parameters = load_robot_configuration(robot_path)
    parameters = CartesianParams.from_mapping({k: v for k, v in values.items() if k != "robot_config"})
    return config, gravity_parameters, parameters


def build_graph(robot, gravity, kinematics, target, commands, parameters):
    graph = hsdk.Graph("rebot-cartesian-hold", default_clock=hsdk.RealtimeClock)
    source = graph.op(ops.joint_state_source(robot), "joint_feedback")
    gravity_op = graph.op(ops.gravity_torque(gravity), "gravity",
                          tau_scale=list(gravity.parameters.tau_scale),
                          joint_direction=list(gravity.parameters.joint_direction))
    kinematics_op = graph.op(ops.pinocchio_kinematics(kinematics), "kinematics",
                             joint_direction=list(gravity.parameters.joint_direction),
                             max_joint_speed=parameters.max_joint_speed,
                             max_cartesian_speed=parameters.max_cartesian_speed)
    impedance = graph.op(ops.cartesian_controller(target), "cartesian_impedance",
                         stiffness=parameters.stiffness, damping=parameters.damping,
                         max_force=parameters.max_force, max_error=parameters.max_error)
    sink = graph.op(ops.motor_command_sink(robot, commands), "motor_commands",
                    joint_damping=parameters.joint_damping, torque_fraction=parameters.torque_fraction)

    graph.add_flow(source.state, gravity_op.state, queue_depth=1, queue_policy=hsdk.QueuePolicy.DROP_OLDEST)
    graph.add_flow(gravity_op.sample, kinematics_op.sample, queue_depth=1, queue_policy=hsdk.QueuePolicy.DROP_OLDEST)
    graph.add_flow(kinematics_op.kinematics, impedance.kinematics, queue_depth=1, queue_policy=hsdk.QueuePolicy.DROP_OLDEST)
    graph.add_flow(impedance.effort, sink.effort, queue_depth=1, queue_policy=hsdk.QueuePolicy.DROP_OLDEST)
    graph.partition("control").add(source, gravity_op, kinematics_op, impedance, sink)
    return graph


class ConsoleInput:
    """Poll complete lines without blocking the watchdog on a partial pipe write."""

    def __init__(self, stream):
        self.buffer = ""
        try:
            self.fd = stream.fileno()
        except (AttributeError, OSError, ValueError):
            self.fd = None

    def poll(self):
        if self.fd is None or not select.select([self.fd], [], [], 0)[0]:
            return []
        data = os.read(self.fd, 1024)
        if not data:
            self.fd = None
            return []
        lines = (self.buffer + data.decode(errors="replace")).split("\n")
        self.buffer = lines.pop()[-1024:]
        return [line.strip().lower() for line in lines if line.strip()]


def run(config_path):
    config, gravity_parameters, parameters = load_configuration(config_path)
    robot = Robot(config)
    gravity = GravityCompensation(config, gravity_parameters)
    kinematics = ToolKinematics(gravity, parameters.end_effector_frame)
    target = HoldTarget()
    commands = CartesianMotorCommand(gravity)
    console = ConsoleInput(sys.stdin)
    stop_requested = threading.Event()

    def request_shutdown(signum, frame):
        stop_requested.set()

    previous_handlers = {sig: signal.signal(sig, request_shutdown) for sig in (signal.SIGINT, signal.SIGTERM)}
    try:
        plan = hsdk.compile(build_graph(robot, gravity, kinematics, target, commands, parameters))
        if not plan:
            raise RuntimeError(plan.summary())
        if stop_requested.is_set():
            return 0
        print(f"Preparing {config.channel} for Cartesian hold at 400 Hz.", flush=True)
        print("Enter h to capture and hold the current point; g to release to gravity compensation.", flush=True)
        print("Release with g, return home manually, then Ctrl+C to disable the motors.", flush=True)
        prepare_cartesian_hold(robot, gravity, kinematics, commands, parameters)
        if stop_requested.is_set():
            return 0
        announced, previous_target = False, None
        with hsdk.run_async(plan) as session:
            while not session.wait(timeout=0.005):
                if stop_requested.is_set():
                    session.request_stop()
                    break
                if time.monotonic_ns() - robot.last_command_ns > config.watchdog_ms * 1_000_000:
                    robot.disable()
                    raise RuntimeError("command completion watchdog expired")
                if robot.commands and not announced:
                    print("Gravity compensation active; hand-position the arm, then enter h.", flush=True)
                    announced = True
                for action in console.poll():
                    if action in ("h", "g"):
                        target.request("hold" if action == "h" else "release")
                    else:
                        print("Use h to hold or g to release, followed by Enter.", flush=True)
                point = target.snapshot()
                if point != previous_target:
                    message = "Gravity compensation active; hold released." if point is None else (
                        "Holding XYZ (m): " + ", ".join(f"{x:.4f}" for x in point))
                    print(message, flush=True)
                    previous_target = point
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
    parser.add_argument("config", nargs="?", default="config/cartesian_hold.yaml", help="Cartesian YAML configuration")
    args = parser.parse_args()
    try:
        return run(args.config)
    except Exception as error:
        print(f"cartesian_hold: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
