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

"""Cartesian math and native Holoscan graph tests; hardware is never opened."""

import contextlib
import ctypes as ct
from dataclasses import replace
import io
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "python"))

import holoscan as hsdk
import numpy as np

from cartesian_hold import ConsoleInput, build_graph, load_configuration, run
from rebot_cartesian import (CartesianMotorCommand, CartesianParams, ControlSample, HoldTarget,
                             ToolKinematics, cartesian_impedance, prepare_cartesian_hold)
from rebot_gravity import GravityCompensation
from rebot_robot import JointState, Robot
from test_python_gravity import FAKE, fake_library

CONFIG = ROOT / "config/cartesian_hold.yaml"


def state(position=(0.1, -0.8, -0.5, 0.2, 0.15, -0.1, 0.0), velocity=(0.0,) * 7):
    return JointState(1, 1_000_000_000, tuple(position), tuple(velocity), (0.0,) * 7, (0,) * 7)


class CartesianMathTests(unittest.TestCase):
    def setUp(self):
        self.config, self.gravity_params, self.params = load_configuration(CONFIG)
        self.gravity = GravityCompensation(self.config, self.gravity_params)
        self.gravity.initialize()
        self.kinematics = ToolKinematics(self.gravity, self.params.end_effector_frame)
        self.kinematics.initialize()
        self.commands = CartesianMotorCommand(self.gravity)

    def kinematic(self, s=None, direction=None):
        return self.kinematics.compute(ControlSample(s or state(), (0.0,) * 7),
                                       joint_direction=direction or self.gravity_params.joint_direction,
                                       max_joint_speed=4.0, max_cartesian_speed=1.0)

    def impedance(self, sample, target, **overrides):
        parameters = dict(stiffness=300.0, damping=12.0, max_force=15.0, max_error=0.1)
        return cartesian_impedance(sample, target, **(parameters | overrides))

    def test_motor_coordinate_jacobian_matches_finite_differences(self):
        # Reverse one motor/model convention and check each Jacobian column numerically.
        direction = (1, -1, 1, 1, 1, 1)
        s = state(position=(0.1, 0.8, -0.5, 0.2, 0.15, -0.1, 0.0))
        sample = self.kinematic(s, direction)
        numerical = np.zeros((3, 6))
        for a, i in enumerate(self.config.arm_indices):
            plus, minus = np.array(s.position), np.array(s.position)
            plus[i] += 1e-6
            minus[i] -= 1e-6
            xp = self.kinematic(replace(s, position=tuple(plus)), direction).tool_position
            xm = self.kinematic(replace(s, position=tuple(minus)), direction).tool_position
            numerical[:, a] = (np.array(xp) - xm) / 2e-6
        np.testing.assert_allclose(sample.jacobian, numerical, atol=1e-8, rtol=0)
        moving = replace(s, velocity=(0.1, -0.2, 0.15, 0.05, 0.1, -0.1, 0.0))
        result = self.kinematic(moving, direction)
        np.testing.assert_allclose(result.tool_velocity, numerical @ np.array(moving.velocity[:6]), atol=1e-8)

    def test_named_joint_order_is_preserved(self):
        reference = self.kinematic()
        config = replace(self.config, arm_indices=tuple(reversed(self.config.arm_indices)))
        reordered = GravityCompensation(config, self.gravity_params)
        reordered.initialize()
        kinematics = ToolKinematics(reordered, "end_link")
        kinematics.initialize()
        actual = kinematics.compute(ControlSample(state(), (0.0,) * 7), joint_direction=(1,) * 6,
                                    max_joint_speed=4.0, max_cartesian_speed=1.0)
        np.testing.assert_allclose(actual.tool_position, reference.tool_position)
        np.testing.assert_allclose(np.array(actual.jacobian)[:, ::-1], reference.jacobian)

    def test_spring_torque_is_negative_gradient_of_cartesian_energy(self):
        s, sample = state(), self.kinematic()
        target = tuple(np.asarray(sample.tool_position) + (0.01, -0.015, 0.005))
        torque = self.impedance(sample, target, damping=0).task_torque
        numerical = []
        for i in self.config.arm_indices:
            energy = []
            for delta in (1e-6, -1e-6):
                q = list(s.position)
                q[i] += delta
                position = self.kinematic(replace(s, position=tuple(q))).tool_position
                energy.append(0.5 * 300 * np.sum((np.asarray(target) - position) ** 2))
            numerical.append(-(energy[0] - energy[1]) / 2e-6)
        np.testing.assert_allclose(torque, numerical, atol=1e-7, rtol=0)

    def test_damping_opposes_motion_and_force_is_bounded(self):
        moving = state(velocity=(0.1, -0.2, 0.15, 0.05, 0.1, -0.1, 0.0))
        sample = self.kinematic(moving)
        command = self.impedance(sample, sample.tool_position)
        self.assertLess(np.dot(command.task_torque, moving.velocity[:6]), 0)
        # An identity translation Jacobian makes the force cap directly observable.
        sample = replace(sample, tool_position=(0, 0, 0), tool_velocity=(0, 0, 0),
                         jacobian=((1, 0, 0, 0, 0, 0), (0, 1, 0, 0, 0, 0), (0, 0, 1, 0, 0, 0)))
        limited = self.impedance(sample, (0.08, 0, 0))
        self.assertAlmostEqual(np.linalg.norm(limited.task_torque), 15.0)

    def test_target_capture_and_release(self):
        target = HoldTarget()
        sample = self.kinematic()
        self.assertIsNone(target.resolve(sample.tool_position))
        target.request("hold")
        held = target.resolve(sample.tool_position)
        self.assertEqual(held, sample.tool_position)
        self.assertEqual(target.resolve((1, 2, 3)), held)
        np.testing.assert_allclose(self.impedance(sample, held).task_torque, 0)
        target.request("release")
        self.assertIsNone(target.resolve((1, 2, 3)))
        released = self.impedance(replace(sample, tool_velocity=(0.2, 0, 0)), None)
        np.testing.assert_allclose(released.task_torque, 0)

    def test_limits_and_nonfinite_values_are_rejected(self):
        sample = self.kinematic()
        with self.assertRaisesRegex(ValueError, "tracking error"):
            self.impedance(sample, tuple(np.asarray(sample.tool_position) + (0.11, 0, 0)))
        with self.assertRaisesRegex(ValueError, "target point"):
            self.impedance(sample, (float("nan"), 0, 0))
        with self.assertRaisesRegex(ValueError, "joint speed"):
            self.kinematic(state(velocity=(4.01, 0, 0, 0, 0, 0, 0)))
        with self.assertRaisesRegex(ValueError, "nonfinite joint velocity"):
            self.kinematic(state(velocity=(float("nan"), 0, 0, 0, 0, 0, 0)))
        with self.assertRaisesRegex(ValueError, "tool speed"):
            self.kinematics.compute(ControlSample(state(velocity=(1, 1, 1, 1, 1, 1, 0)), (0.0,) * 7),
                                    joint_direction=(1,) * 6, max_joint_speed=4, max_cartesian_speed=0.001)

    def test_sink_scales_task_torque_and_accounts_for_motor_damping(self):
        s = state(velocity=(0.2, -0.1, 0.2, 0.1, -0.1, 0.2, 0))
        g = self.gravity.gravity_torques(s, tau_scale=(1,) * 6, joint_direction=(1,) * 6,
                                        now_ns=s.timestamp_ns)
        sample = replace(self.kinematic(s), gravity_torque=g, task_torque=(100, -80, 40, -50, 80, 60))
        command = self.commands.compute(sample, joint_damping=1.25, torque_fraction=0.8)
        ff = np.asarray(command.torque[:6])
        limit = self.gravity.effort * 0.8
        self.assertTrue(np.all(np.abs(ff) <= limit + 1e-12))
        self.assertTrue(np.all(np.abs(ff - 1.25 * np.asarray(s.velocity[:6])) <= limit + 1e-12))
        scales = (ff - g[:6]) / sample.task_torque
        self.assertGreater(scales[0], 0)
        self.assertLess(scales[0], 1)
        np.testing.assert_allclose(scales, scales[0], atol=1e-12)
        self.assertEqual(command.kp[:6], (0,) * 6)
        self.assertEqual(command.kd[:6], (1.25,) * 6)
        self.assertEqual((command.kp[6], command.kd[6], command.torque[6]), (8, 1, 0))
        self.assertEqual((command.sequence, command.timestamp_ns), (s.sequence, s.timestamp_ns))
        with self.assertRaisesRegex(ValueError, "torque budget"):
            self.commands.compute(replace(sample, gravity_torque=(100,) * 7),
                                  joint_damping=1, torque_fraction=0.8)
        with self.assertRaisesRegex(ValueError, "joint torque"):
            self.commands.compute(replace(sample, task_torque=(float("nan"),) * 6),
                                  joint_damping=1, torque_fraction=0.8)

    def test_cartesian_parameter_validation(self):
        for invalid in ({"stiffness": -1}, {"damping": float("nan")}, {"joint_damping": 6},
                        {"torque_fraction": 1.1}, {"max_force": 0}, {"end_effector_frame": ""}, {"typo": 1}):
            with self.subTest(invalid=invalid), self.assertRaises(ValueError):
                CartesianParams.from_mapping(invalid)


class CartesianGraphTests(unittest.TestCase):
    def setUp(self):
        self.fake = fake_library()
        self.fake.fake_first_kp.restype = ct.c_float
        self.fake.fake_reset()
        self.enterContext(patch.dict(os.environ, MOTORBRIDGE_LIB=str(FAKE)))
        config, gp, self.params = load_configuration(CONFIG)
        self.robot = Robot(config)
        self.addCleanup(self.robot.close)
        self.gravity = GravityCompensation(config, gp)
        self.kinematics = ToolKinematics(self.gravity, self.params.end_effector_frame)
        self.target = HoldTarget()
        self.commands = CartesianMotorCommand(self.gravity)

    def prepare(self):
        plan = hsdk.compile(build_graph(self.robot, self.gravity, self.kinematics,
                                      self.target, self.commands, self.params))
        self.assertTrue(plan, plan.summary())
        self.assertEqual(self.fake.fake_writes(), 0, "compilation must not touch motors")
        prepare_cartesian_hold(self.robot, self.gravity, self.kinematics, self.commands, self.params)
        self.assertEqual(self.fake.fake_first_kp(), 0)
        self.assertEqual(self.fake.fake_enables(), 7)
        self.assertGreater(self.robot.last_command_ns, 0)
        return plan

    def wait_for_commands(self, session, count):
        deadline = time.monotonic() + 3
        while self.robot.commands < count:
            self.assertFalse(session.wait(timeout=0.005), str(session.diagnostics))
            self.assertLess(time.monotonic(), deadline, "Cartesian graph stalled")

    def test_graph_hold_release_parameters_and_cleanup(self):
        self.params = replace(self.params, joint_damping=1.25)
        sent = []
        send = self.robot.send_command

        def record(command):
            sent.append(command)
            send(command)

        self.enterContext(patch.object(self.robot, "send_command", record))
        plan = self.prepare()
        with hsdk.run_async(plan) as session:
            self.wait_for_commands(session, 30)
            self.target.request("hold")
            self.wait_for_commands(session, self.robot.commands + 10)
            self.assertIsNotNone(self.target.snapshot())
            self.target.request("release")
            self.wait_for_commands(session, self.robot.commands + 10)
            self.assertIsNone(self.target.snapshot())
            session.request_stop()
        self.assertFalse(session.diagnostics)
        self.assertIs(session.termination, hsdk.RunTermination.STOPPED)
        self.assertEqual(self.fake.fake_disables(), 7)
        self.assertEqual(sent[0].kd[:6], (1.25,) * 6)
        self.assertTrue(all(b.sequence > a.sequence for a, b in zip(sent, sent[1:])))

    def test_transport_failure_disables_owned_motors(self):
        plan = self.prepare()
        with hsdk.run_async(plan) as session:
            self.wait_for_commands(session, 10)
            self.fake.fake_send_error(1)
            self.assertTrue(session.wait(timeout=3))
        self.assertTrue(session.diagnostics)
        self.assertEqual(self.fake.fake_disables(), 7)

    def test_sink_budget_failure_disables_without_sending_bad_batch(self):
        plan = self.prepare()
        with hsdk.run_async(plan) as session:
            self.wait_for_commands(session, 10)
            with patch.object(self.commands, "compute", side_effect=ValueError("torque budget")):
                self.assertTrue(session.wait(timeout=3))
        self.assertTrue(session.diagnostics)
        self.assertEqual(self.fake.fake_disables(), 7)

    def test_invalid_frame_fails_before_device_open(self):
        self.kinematics.frame_name = "missing_frame"
        with self.assertRaisesRegex(ValueError, "unknown tool frame"):
            prepare_cartesian_hold(self.robot, self.gravity, self.kinematics, self.commands, self.params)
        self.assertIsNone(self.robot.controller)
        self.assertEqual(self.fake.fake_writes(), 0)

    def test_watchdog_covers_missing_first_command(self):
        with patch.object(Robot, "send_command", lambda robot, command: None), \
                patch.object(ConsoleInput, "poll", return_value=[]), contextlib.redirect_stdout(io.StringIO()):
            with self.assertRaisesRegex(RuntimeError, "command completion watchdog"):
                run(CONFIG)
        self.assertEqual(self.fake.fake_disables(), 7)


class CartesianCliTests(unittest.TestCase):
    def setUp(self):
        fake_library()  # Require test-only symbols before starting any subprocess.

    def test_help(self):
        result = subprocess.run([sys.executable, str(ROOT / "python/cartesian_hold.py"), "--help"],
                                env=dict(os.environ, MOTORBRIDGE_LIB=str(FAKE)),
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Cartesian", result.stdout)

    def test_interactive_capture_release_and_signals(self):
        for stop_signal in (signal.SIGINT, signal.SIGTERM):
            with self.subTest(signal=stop_signal), tempfile.TemporaryDirectory() as directory:
                log = Path(directory) / "cartesian.log"
                with log.open("w") as output:
                    process = subprocess.Popen([sys.executable, str(ROOT / "python/cartesian_hold.py"), str(CONFIG)],
                                               env=dict(os.environ, MOTORBRIDGE_LIB=str(FAKE)),
                                               stdin=subprocess.PIPE, stdout=output, stderr=output)

                    def wait_for(text):
                        deadline = time.monotonic() + 5
                        while text not in log.read_text():
                            self.assertIsNone(process.poll(), log.read_text())
                            self.assertLess(time.monotonic(), deadline, log.read_text())
                            time.sleep(0.01)

                    try:
                        wait_for("Gravity compensation active;")
                        process.stdin.write(b"h")
                        process.stdin.flush()
                        time.sleep(0.15)  # A partial line must not block the watchdog or graph.
                        self.assertIsNone(process.poll(), log.read_text())
                        self.assertNotIn("Holding XYZ", log.read_text())
                        process.stdin.write(b"\n")
                        process.stdin.flush()
                        wait_for("Holding XYZ")
                        process.stdin.write(b"g\n")
                        process.stdin.flush()
                        wait_for("hold released")
                        process.send_signal(stop_signal)
                        self.assertEqual(process.wait(timeout=3), 0, log.read_text())
                        self.assertRegex(log.read_text(), r"Stopped after [1-9][0-9]* command batches; motors disabled")
                    finally:
                        process.stdin.close()
                        if process.poll() is None:
                            process.kill()
                            process.wait()


if __name__ == "__main__":
    unittest.main(verbosity=2)
