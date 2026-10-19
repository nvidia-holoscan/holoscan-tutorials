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

"""Damiao configuration and hardware control through the motorbridge Python SDK.

The lifecycle and checks mirror src/robot.cpp. Nothing connects at import time.
Feedback timestamps date host cache reads, not receipt of motor packets.
"""

from dataclasses import dataclass
import math
from pathlib import Path
import threading
import time

from motorbridge import Controller, Mode
from motorbridge.errors import CallError


def require(condition, message):
    if not condition:
        raise ValueError(message)


def number(value, label, low, high):
    require(type(value) in (int, float) and math.isfinite(value)
            and low <= value <= high, f"{label} must be finite and in [{low}, {high}]")
    return float(value)


def integer(value, label, low, high):
    require(type(value) is int and low <= value <= high,
            f"{label} must be an integer in [{low}, {high}]")
    return value


@dataclass(frozen=True)
class JointConfig:
    name: str
    motor_id: int
    feedback_id: int
    model: str
    kp: float
    kd: float
    velocity_limit: float
    torque_limit: float


@dataclass(frozen=True)
class RobotConfig:
    channel: str
    urdf_path: str
    joints: tuple[JointConfig, ...]
    arm_indices: tuple[int, ...]
    baud: int = 921600
    watchdog_ms: int = 100


def robot_config(values, config_path):
    """Validate the same hardware fields and path convention as the C++ demo."""
    require("rate" not in values, "Control runs at a fixed 400 Hz; remove 'rate'")
    channel = values["channel"]
    require(isinstance(channel, str) and channel, "channel must not be empty")
    baud = integer(values.get("baud", 921600), "baud", 1, 2**32 - 1)
    watchdog = integer(values.get("watchdog_ms", 100), "watchdog_ms", 20, 1000)
    urdf = Path(values["urdf_path"])
    if not urdf.is_absolute():
        parent = Path(config_path).resolve().parent
        urdf = parent / urdf if (parent / urdf).is_file() else parent.parent / urdf
    require(urdf.is_file(), f"URDF does not exist: {urdf}")
    names, motor_ids, feedback_ids, joints = set(), set(), set(), []
    require(isinstance(values["joints"], list) and values["joints"],
            "joints must be a nonempty sequence")
    for item in values["joints"]:
        name = item["name"]
        require(isinstance(name, str) and name and name not in names,
                f"duplicate or empty joint name: {name}")
        motor = integer(item["motor_id"], "motor_id", 1, 255)
        feedback = integer(item["feedback_id"], "feedback_id", 0, 0x7FF)
        require(motor not in motor_ids and feedback not in feedback_ids,
                f"duplicate motor or feedback ID: {name}")
        require(item.get("vendor", "damiao") == "damiao", "Damiao motors only")
        model = item["model"]
        require(model in ("4340P", "4340", "4310"), f"unsupported model: {model}")
        gains = item.get("MIT", {})
        joints.append(JointConfig(
            name, motor, feedback, model,
            number(gains.get("kp", 0.0), "MIT kp", 0, 500),
            number(gains.get("kd", 0.0), "MIT kd", 0, 5),
            30.0 if model == "4310" else 10.0,
            10.0 if model == "4310" else 28.0))
        names.add(name)
        motor_ids.add(motor)
        feedback_ids.add(feedback)
    index = {joint.name: i for i, joint in enumerate(joints)}
    grouped, arm = set(), []
    for group in ("arm", "gripper"):
        group_names = values.get("groups", {}).get(group, {}).get("joints", [])
        require(isinstance(group_names, list), "group joints must be a sequence")
        for name in group_names:
            require(name in index and name not in grouped,
                    f"unknown or multiply grouped joint: {name}")
            grouped.add(name)
            if group == "arm":
                arm.append(index[name])
    require(len(arm) == 6, "Gravity compensation requires six arm joints")
    require(grouped == names, "every joint must be in arm or gripper group")
    return RobotConfig(channel, str(urdf.resolve()), tuple(joints), tuple(arm), baud, watchdog)


@dataclass(frozen=True)
class JointState:
    sequence: int
    timestamp_ns: int
    position: tuple[float, ...]
    velocity: tuple[float, ...]
    torque: tuple[float, ...]
    status: tuple[int, ...]


@dataclass(frozen=True)
class MotorCommand:
    sequence: int
    timestamp_ns: int
    position: tuple[float, ...]
    velocity: tuple[float, ...]
    kp: tuple[float, ...]
    kd: tuple[float, ...]
    torque: tuple[float, ...]


class Robot:
    """Own one connection; disable only motors this instance has enabled."""

    def __init__(self, config):
        self.config = config
        self.lock = threading.RLock()
        self.controller = None
        self.motors, self.owned = [], []
        self.last_read_ns = self.last_command_ns = self.commands = 0
        self.previous_sequence = 0

    def connect(self):
        with self.lock:
            require(self.controller is None, "robot is already connected")
            try:
                self.controller = Controller.from_dm_serial(self.config.channel, self.config.baud)
                for joint in self.config.joints:
                    motor = self.controller.add_damiao_motor(joint.motor_id, joint.feedback_id, joint.model)
                    self.motors.append(motor)
                    self.owned.append(False)
            except BaseException:
                self.close()
                raise

    def read_state(self, sequence=0):
        with self.lock:
            require(self.controller is not None, "robot is not connected")
            timeout_ns = self.config.watchdog_ms * 1_000_000 if any(self.owned) else 250_000_000
            deadline = time.monotonic_ns() + timeout_ns
            try:
                for motor, owned in zip(self.motors, self.owned):
                    if not owned:
                        motor.request_feedback()
                while True:
                    self.controller.poll_feedback_once()
                    timestamp = time.monotonic_ns()
                    states, ready = [], True
                    for motor, owned, joint in zip(self.motors, self.owned, self.config.joints):
                        state = motor.get_state()
                        if state is None:
                            ready = False
                        else:
                            require(all(math.isfinite(v) for v in (state.pos, state.vel, state.torq)),
                                    f"nonfinite feedback: {joint.name}")
                            require(state.status_code in (0, 1),
                                    f"DM motor fault {joint.name} status={state.status_code}")
                            ready = ready and (not owned or state.status_code == 1)
                        states.append(state)
                    require(time.monotonic_ns() < deadline, "feedback unavailable or motor disabled")
                    if ready:
                        self.last_read_ns = timestamp
                        return JointState(sequence, timestamp,
                                          tuple(s.pos for s in states), tuple(s.vel for s in states),
                                          tuple(s.torq for s in states), tuple(s.status_code for s in states))
                    time.sleep(0.0001)
            except BaseException:
                self.disable()
                raise

    def validate_command(self, command):
        fields = (command.position, command.velocity, command.kp, command.kd, command.torque)
        require(all(len(field) == len(self.motors) for field in fields),
                "MIT command must contain every configured joint")
        for i, joint in enumerate(self.config.joints):
            for field, limit in zip(fields, (12.5, joint.velocity_limit, 500, 5, joint.torque_limit)):
                require(math.isfinite(field[i]) and abs(field[i]) <= limit,
                        f"MIT command exceeds finite motor limits: {joint.name}")
            require(command.kp[i] >= 0 and command.kd[i] >= 0, "MIT gains must be nonnegative")

    def _send_one(self, i, command):
        self.motors[i].send_mit(
            pos=command.position[i], vel=command.velocity[i],
            kp=command.kp[i], kd=command.kd[i], tau=command.torque[i])

    def prepare_mit(self, initial):
        with self.lock:
            require(self.controller is not None and not any(self.owned), "motors already enabled or disconnected")
            self.validate_command(initial)
            measured = self.read_state()
            require(all(status == 0 for status in measured.status), "motor already enabled by another controller")
            require(all(abs(a - b) < 0.15 for a, b in zip(initial.position, measured.position)),
                    "initial MIT target is not the current measured position")
            try:
                for motor in self.motors:
                    motor.ensure_mode(Mode.MIT, 1000)
                    motor.set_can_timeout_ms(self.config.watchdog_ms)
                    timeout = motor.get_register_u32(9, 100)
                    require(timeout == self.config.watchdog_ms * 20, "CAN watchdog verification failed")
                measured = self.read_state()
                require(all(abs(a - b) < 0.15 for a, b in zip(initial.position, measured.position)),
                        "robot moved during MIT setup; rerun from its current pose")
                for i, motor in enumerate(self.motors):
                    self._send_one(i, initial)
                    self.owned[i] = True  # Cleanup owns even a partially failed enable.
                    motor.enable()
                    self._send_one(i, initial)
            except BaseException:
                self.disable()
                raise

    def send_command(self, command):
        with self.lock:
            try:
                now = time.monotonic_ns()
                max_age = self.config.watchdog_ms * 1_000_000
                require(command.sequence > self.previous_sequence, "out-of-order motor command")
                require(0 < command.timestamp_ns <= now and now - command.timestamp_ns <= max_age,
                        "expired or invalid command timestamp")
                require(self.last_read_ns > 0 and now - self.last_read_ns < max_age,
                        "joint state has not been read recently")
                require(any(self.owned), "no motors enabled by this instance")
                self.validate_command(command)  # Validate the whole batch before transmitting.
                for i, owned in enumerate(self.owned):
                    if owned:
                        self._send_one(i, command)
                self.previous_sequence = command.sequence
                self.last_command_ns = time.monotonic_ns()
                self.commands += 1
            except BaseException:
                self.disable()
                raise

    def disable(self):
        with self.lock:
            for i, owned in enumerate(self.owned):
                if owned:
                    for _ in range(3):
                        try:
                            self.motors[i].disable()
                            break
                        except CallError:
                            continue
                    self.owned[i] = False

    def close(self):
        with self.lock:
            self.disable()
            if self.controller:
                # shutdown() would also disable motors this instance does not own.
                try:
                    self.controller.close_bus()
                except CallError:
                    pass  # Still release handles if the transport has already failed.
                for motor in self.motors:
                    motor.close()
                self.controller.close()
            self.controller = None
            self.motors.clear()
            self.owned.clear()
