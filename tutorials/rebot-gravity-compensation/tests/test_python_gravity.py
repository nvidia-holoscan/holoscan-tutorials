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

"""Exercise the Python demo against the same fake C ABI as the C++ tests.

Build rebot_fake_motor_abi first, then run this file with the Holoscan venv.
These tests never select the real motorbridge library or open the hardware bus.
"""

import contextlib
import ctypes as ct
from dataclasses import replace
import io
import json
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
import yaml

from gravity_compensation import build_graph, load_configuration, run
from rebot_gravity import GravityCompensation, GravityParams, prepare_gravity_compensation
from rebot_robot import JointState, Robot

FAKE = Path(os.environ.get("REBOT_TEST_MOTOR_LIBRARY", ROOT / "build/librebot_fake_motor_abi.so")).resolve()
CONFIG = ROOT / "config/rebotarm_dm.yaml"


def fake_library():
    if not FAKE.is_file():
        raise RuntimeError(f"Build the fake driver first: {FAKE}")
    library = ct.CDLL(str(FAKE))
    # Verify the test-only symbols before any CLI subprocess could open a bus.
    for name in ("fake_reset", "fake_enables", "fake_disables", "fake_send_error"):
        getattr(library, name)
    return library


class GravityTests(unittest.TestCase):
    def setUp(self):
        self.config, self.parameters = load_configuration(CONFIG)
        self.gravity = GravityCompensation(self.config, self.parameters)
        self.gravity.initialize()

    def state(self, sequence=1, timestamp=1_000_000_000, position=None):
        q = (0.1, -0.8, -0.5, 0.2, 0.15, -0.1, 0.0) if position is None else tuple(position)
        return JointState(sequence, timestamp, q, (0.0,) * 7, (0.0,) * 7, (0,) * 7)

    def compute(self, state, **overrides):
        return self.gravity.compute(state, now_ns=state.timestamp_ns,
                                    **(self.parameters.operator_kwargs() | overrides))

    def test_reference_torques_and_gripper(self):
        # Same reference used by tests/test_dynamics.cpp; no motor is involved.
        command = self.compute(self.state())
        expected = (3.35482618248062e-18, 1.3508952882965248, -7.221099958260734,
                    -1.8440316884303196, 0.05076182554779114, 0.00028107511739617675)
        np.testing.assert_allclose(command.torque[:6], expected, atol=1e-10, rtol=0)
        self.assertEqual(command.torque[6], 0.0)
        self.assertEqual(command.kp[6], 8.0)
        self.assertEqual(command.kd[6], 1.0)

    def test_joint_direction_and_torque_scale(self):
        q = self.state().position
        reference = self.compute(self.state()).torque
        self.gravity = GravityCompensation(self.config, self.parameters)
        self.gravity.initialize()
        motor_q = (q[0], -q[1], *q[2:])
        result = self.compute(self.state(position=motor_q),
                              joint_direction=[1, -1, 1, 1, 1, 1], tau_scale=[0.5] * 6)
        np.testing.assert_allclose(result.torque[:6],
                                   np.array(reference[:6]) * [1, -1, 1, 1, 1, 1] * 0.5, atol=1e-10)

    def test_named_joint_mapping(self):
        reference = self.compute(self.state()).torque
        self.gravity = GravityCompensation(replace(self.config, arm_indices=(5, 4, 3, 2, 1, 0)), self.parameters)
        self.gravity.initialize()
        np.testing.assert_allclose(self.compute(self.state()).torque, reference, atol=1e-10)

    def test_direct_startup_uses_final_gains_and_tracks_measured_pose(self):
        initial = self.state()
        start = self.compute(initial)
        np.testing.assert_allclose(start.kp[:6], [1.5] * 6)
        np.testing.assert_allclose(start.kd[:6], [1.0] * 6)
        np.testing.assert_allclose(start.position, initial.position)
        moved = (0.2, -0.9, -0.6, 0.25, 0.2, -0.15, 0.0)
        next_command = self.compute(self.state(2, initial.timestamp_ns + 2_500_000, moved))
        np.testing.assert_allclose(next_command.kp, start.kp)
        np.testing.assert_allclose(next_command.kd, start.kd)
        np.testing.assert_allclose(next_command.position, moved)

    def test_rejects_expired_out_of_order_and_nonfinite_state(self):
        state = self.state()
        self.compute(state)
        with self.assertRaisesRegex(ValueError, "out-of-order"):
            self.compute(state)
        with self.assertRaisesRegex(ValueError, "timestamp"):
            self.gravity.compute(replace(state, sequence=2), now_ns=state.timestamp_ns + 100_000_001,
                                 **self.parameters.operator_kwargs())
        with self.assertRaisesRegex(ValueError, "nonfinite"):
            self.compute(self.state(2, position=(float("nan"), *state.position[1:])))

    def test_rejects_position_and_effort_limit_violations(self):
        with self.assertRaisesRegex(ValueError, "URDF limits"):
            self.compute(self.state(position=(0, 1, 0, 0, 0, 0, 0)))
        with self.assertRaisesRegex(ValueError, "torque exceeds"):
            self.compute(self.state(), tau_scale=[100] * 6)

    def test_parameter_and_yaml_validation(self):
        for invalid in ({"kp": -1}, {"kd": 6}, {"tau_scale": [1]},
                        {"joint_direction": [0] * 6}, {"transition_duration": 0.0}, {"typo": 1}):
            with self.subTest(invalid=invalid), self.assertRaises(ValueError):
                GravityParams.from_mapping(invalid)
        text = CONFIG.read_text()
        for invalid in (text + "\nchannel: /dev/duplicate\n", text.replace("motor_id: 1", "motor_id: 0x1", 1)):
            with tempfile.TemporaryDirectory() as directory:
                path = Path(directory) / "invalid.yaml"
                path.write_text(invalid)
                with self.assertRaisesRegex(hsdk.HoloscanError, "CONFIG_DUPLICATE_KEY|CONFIG_UNSUPPORTED_VALUE"):
                    load_configuration(path)


class HardwareGraphTests(unittest.TestCase):
    def setUp(self):
        self.fake = fake_library()
        self.fake.fake_first_kp.restype = ct.c_float
        self.fake.fake_reset()
        self.config, self.parameters = load_configuration(CONFIG)
        self.enterContext(patch.dict(os.environ, MOTORBRIDGE_LIB=str(FAKE)))
        self.robot = Robot(self.config)
        self.addCleanup(self.robot.close)

    def prepare(self, parameters=None):
        controller = GravityCompensation(self.robot.config, parameters or self.parameters)
        plan = hsdk.compile(build_graph(self.robot, controller))
        self.assertTrue(plan, plan.summary())
        self.assertEqual(self.fake.fake_writes(), 0, "compilation must not touch motors")
        prepare_gravity_compensation(self.robot, controller)
        self.assertEqual(self.fake.fake_enables(), 7)
        self.assertAlmostEqual(self.fake.fake_first_kp(), controller.parameters.kp,
                               msg="initial command must use the configured compliant gain")
        self.assertGreater(self.robot.last_command_ns, 0)
        self.assertEqual(self.robot.commands, 0)
        return plan

    def wait_for_commands(self, session, count=30):
        deadline = time.monotonic() + 3
        while self.robot.commands < count:
            self.assertFalse(session.wait(timeout=0.005), str(session.diagnostics))
            self.assertLess(time.monotonic(), deadline, "graph did not make progress")

    def test_graph_parameters_and_normal_cleanup(self):
        # Pass non-default YAML values all the way through native hsdk.Param to the driver.
        values = yaml.safe_load(CONFIG.read_text())
        values["urdf_path"] = self.config.urdf_path
        values["gravity_compensation"].update(kp=2.25, kd=1.25)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "custom.yaml"
            # JSON is valid in the SDK's YAML subset and keeps model strings quoted.
            path.write_text(json.dumps(values))
            _, parameters = load_configuration(path)
        plan = self.prepare(parameters)
        with hsdk.run_async(plan) as session:
            self.wait_for_commands(session)
            self.assertAlmostEqual(self.fake.fake_first_kp(), 2.25)
            session.request_stop()
        self.assertIs(session.termination, hsdk.RunTermination.STOPPED)
        self.assertFalse(session.diagnostics)
        self.assertEqual(self.fake.fake_disables(), 7)

    def test_transport_failure_stops_graph_and_disables(self):
        plan = self.prepare()
        with hsdk.run_async(plan) as session:
            self.wait_for_commands(session)
            self.fake.fake_send_error(1)
            self.assertTrue(session.wait(timeout=3))
        self.assertIsNot(session.termination, hsdk.RunTermination.STOPPED)
        self.assertTrue(session.diagnostics)
        self.assertEqual(self.fake.fake_disables(), 7)

    def test_bad_model_rejected_before_motor_writes(self):
        controller = GravityCompensation(replace(self.config, urdf_path="/missing/rebot.urdf"), self.parameters)
        self.assertTrue(hsdk.compile(build_graph(self.robot, controller)))
        with self.assertRaises(Exception):
            prepare_gravity_compensation(self.robot, controller)
        self.assertEqual(self.fake.fake_writes(), 0)
        self.assertIsNone(self.robot.controller)

    def test_refuses_unowned_enabled_motors(self):
        self.robot.connect()
        self.fake.fake_set_status(0, 1)
        controller = GravityCompensation(self.config, self.parameters)
        controller.initialize()
        initial = controller.compute(self.robot.read_state(), **self.parameters.operator_kwargs())
        with self.assertRaisesRegex(ValueError, "already enabled"):
            self.robot.prepare_mit(initial)
        self.robot.close()
        self.assertEqual(self.fake.fake_writes(), 0)

    def test_rejects_complete_batch_before_any_transmission(self):
        self.prepare()
        controller = GravityCompensation(self.config, self.parameters)
        controller.initialize()
        command = controller.compute(self.robot.read_state(1), **self.parameters.operator_kwargs())
        invalid = replace(command, torque=(*command.torque[:-1], float("nan")))
        before = self.fake.fake_mit_writes()
        with self.assertRaisesRegex(ValueError, "finite motor limits"):
            self.robot.send_command(invalid)
        self.assertEqual(self.fake.fake_mit_writes(), before)
        self.assertEqual(self.fake.fake_disables(), 7)

    def test_stock_feedback_cache_limitation(self):
        self.prepare()
        before = self.robot.read_state(1)
        self.fake.fake_freeze(1)
        self.fake.fake_set_status(0, 13)  # Real fault is hidden by the frozen cache.
        after = self.robot.read_state(2)
        self.assertEqual(before.position, after.position)
        self.assertEqual(after.status[0], 1)
        self.assertGreater(after.timestamp_ns, before.timestamp_ns)

    def test_motor_fault_disables_owned_motors(self):
        self.prepare()
        self.fake.fake_set_status(0, 13)
        with self.assertRaisesRegex(ValueError, "status=13"):
            self.robot.read_state(1)
        self.assertEqual(self.fake.fake_disables(), 7)

    def test_cleanup_continues_after_unexpected_disable_error(self):
        self.prepare()
        self.fake.fake_set_status(0, 13)
        with patch.object(self.robot.motors[0], "disable",
                          side_effect=RuntimeError("injected cleanup failure")) as disable:
            with self.assertRaisesRegex(ValueError, "status=13"):
                self.robot.read_state(1)
        self.assertEqual(disable.call_count, 3)
        self.assertEqual(self.fake.fake_disables(), 6)
        self.assertFalse(any(self.robot.owned))

    def test_host_watchdog_covers_missing_first_graph_command(self):
        with patch.dict(os.environ, MOTORBRIDGE_LIB=str(FAKE)), \
                patch.object(Robot, "send_command", lambda robot, command: None), \
                contextlib.redirect_stdout(io.StringIO()):
            with self.assertRaisesRegex(RuntimeError, "command completion watchdog"):
                run(CONFIG)
        self.assertEqual(self.fake.fake_enables(), 7)
        self.assertEqual(self.fake.fake_disables(), 7)


class CliTests(unittest.TestCase):
    def setUp(self):
        fake_library()

    def test_help_and_removed_modes(self):
        environment = dict(os.environ, MOTORBRIDGE_LIB=str(FAKE))
        for flag, expected in (("--help", 0), ("--simulate", 2), ("--probe", 2)):
            with self.subTest(flag=flag):
                result = subprocess.run([sys.executable, str(ROOT / "python/gravity_compensation.py"), flag],
                                        env=environment, capture_output=True, text=True, timeout=10)
                self.assertEqual(result.returncode, expected, result.stderr)

    def test_sigint_and_sigterm(self):
        environment = dict(os.environ, MOTORBRIDGE_LIB=str(FAKE))
        for stop_signal in (signal.SIGINT, signal.SIGTERM):
            with self.subTest(signal=stop_signal), tempfile.TemporaryDirectory() as directory:
                log = Path(directory) / "run.log"
                with log.open("w") as output:
                    process = subprocess.Popen(
                        [sys.executable, str(ROOT / "python/gravity_compensation.py"), str(CONFIG)],
                        env=environment, stdout=output, stderr=output)
                    try:
                        deadline = time.monotonic() + 5
                        while "Gravity compensation active." not in log.read_text():
                            self.assertIsNone(process.poll(), log.read_text())
                            self.assertLess(time.monotonic(), deadline, log.read_text())
                            time.sleep(0.01)
                        process.send_signal(stop_signal)
                        self.assertEqual(process.wait(timeout=3), 0, log.read_text())
                        self.assertRegex(log.read_text(), r"Stopped after [1-9][0-9]* command batches; motors disabled")
                    finally:
                        if process.poll() is None:
                            process.kill()
                            process.wait()


if __name__ == "__main__":
    unittest.main(verbosity=2)
