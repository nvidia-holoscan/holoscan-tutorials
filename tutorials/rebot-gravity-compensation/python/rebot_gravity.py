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

"""Pinocchio gravity feedforward with fixed compliant gains."""

from dataclasses import dataclass, fields
import sys
import time

import numpy as np
import pinocchio as pin

from rebot_robot import MotorCommand, number, require


@dataclass(frozen=True)
class GravityParams:
    kp: float = 1.5
    kd: float = 1.0
    tau_scale: tuple[float, ...] = (1.0,) * 6
    joint_direction: tuple[float, ...] = (1.0,) * 6

    @classmethod
    def from_mapping(cls, values):
        require(isinstance(values, dict), "gravity_compensation must be a mapping")
        require(not (values.keys() - {field.name for field in fields(cls)}),
                "unknown gravity_compensation parameter")
        defaults = cls()
        kp = number(values.get("kp", defaults.kp), "kp", 0, 500)
        kd = number(values.get("kd", defaults.kd), "kd", 0, 5)
        vectors = []
        for key in ("tau_scale", "joint_direction"):
            vector = values.get(key, getattr(defaults, key))
            require(isinstance(vector, (list, tuple)) and len(vector) == 6,
                    f"{key} must contain exactly six numbers")
            vectors.append(tuple(number(v, key, -sys.float_info.max, sys.float_info.max) for v in vector))
        require(all(v in (-1, 1) for v in vectors[1]), "joint_direction values must be +1 or -1")
        return cls(kp, kd, *vectors)

    def operator_kwargs(self):
        return dict(kp=self.kp, kd=self.kd,
                    tau_scale=list(self.tau_scale), joint_direction=list(self.joint_direction))


class GravityCompensation:
    """One model, prepared before this graph's single run."""

    def __init__(self, config, parameters):
        self.config, self.parameters = config, parameters
        self.model = None
        self.previous_sequence = None
        self.previous_state_ns = 0

    def initialize(self):
        require(self.model is None, "gravity compensation is already initialized")
        self.model = pin.buildModelFromUrdf(self.config.urdf_path)
        self.model.gravity.linear = np.array([0.0, 0.0, -9.81])
        self.model.gravity.angular = np.zeros(3)
        self.data = self.model.createData()
        self.q = pin.neutral(self.model)
        self.q_indices, self.v_indices = [], []
        self.arm_indices = list(self.config.arm_indices)
        arm = [self.config.joints[i] for i in self.arm_indices]
        for motor in arm:
            require(self.model.existJointName(motor.name) and motor.name != "universe",
                    f"controlled joint is absent from URDF: {motor.name}")
            joint = self.model.joints[self.model.getJointId(motor.name)]
            require(joint.nq == 1 and joint.nv == 1, "controlled joints must have one position and velocity")
            self.q_indices.append(joint.idx_q)
            self.v_indices.append(joint.idx_v)
        self.lower = self.model.lowerPositionLimit[self.q_indices]
        self.upper = self.model.upperPositionLimit[self.q_indices]
        self.effort = np.minimum(self.model.effortLimit[self.v_indices], [j.torque_limit for j in arm])
        require(np.all(np.isfinite(self.effort)) and np.all(self.effort > 0), "invalid URDF effort limits")

    def gravity_torques(self, state, *, tau_scale, joint_direction, now_ns=None):
        """Return motor-ordered gravity torque, independently of command packing."""
        require(self.model is not None, "gravity compensation has not been initialized")
        now = time.monotonic_ns() if now_ns is None else now_ns
        require(len(state.position) == len(self.config.joints), "joint state size does not match configuration")
        require(0 < state.timestamp_ns <= now and
                now - state.timestamp_ns <= self.config.watchdog_ms * 1_000_000,
                "expired or invalid state timestamp")
        require(self.previous_sequence is None or
                (state.sequence > self.previous_sequence and state.timestamp_ns >= self.previous_state_ns),
                "out-of-order control sample")
        measured = np.array(state.position)[self.arm_indices]
        model_q = measured * joint_direction
        require(np.all(np.isfinite(model_q)), "nonfinite joint position")
        require(np.all(model_q >= self.lower - 0.035) and np.all(model_q <= self.upper + 0.035),
                "joint position outside URDF limits")
        self.q[self.q_indices] = model_q  # Other modeled joints remain at neutral.
        gravity = pin.computeGeneralizedGravity(self.model, self.data, self.q)[self.v_indices]
        gravity = gravity * joint_direction * tau_scale
        require(np.all(np.isfinite(gravity)) and np.all(np.abs(gravity) <= self.effort),
                "nonfinite gravity torque or gravity torque exceeds limit")
        self.previous_sequence, self.previous_state_ns = state.sequence, state.timestamp_ns
        torque = [0.0] * len(state.position)
        for j, i in enumerate(self.arm_indices):
            torque[i] = float(gravity[j])
        return tuple(torque)

    def compute(self, state, *, kp, kd, tau_scale, joint_direction, now_ns=None):
        torque = self.gravity_torques(state, tau_scale=tau_scale,
                                     joint_direction=joint_direction, now_ns=now_ns)
        command_kp = [j.kp for j in self.config.joints]
        command_kd = [j.kd for j in self.config.joints]
        for i in self.arm_indices:
            command_kp[i], command_kd[i] = kp, kd
        return MotorCommand(state.sequence, state.timestamp_ns, state.position, (0.0,) * len(state.position),
                            tuple(command_kp), tuple(command_kd), torque)


def prepare_gravity_compensation(robot, gravity):
    """After compile, before run: prepare motors with gravity torque and final gains."""
    gravity.initialize()  # Invalid models fail before opening the bus.
    robot.connect()
    state = robot.read_state(sequence=0)  # Graph samples start at 1.
    initial = gravity.compute(state, **gravity.parameters.operator_kwargs())
    robot.prepare_mit(initial)
    robot.last_command_ns = time.monotonic_ns()  # Include the gap before the first graph command.
