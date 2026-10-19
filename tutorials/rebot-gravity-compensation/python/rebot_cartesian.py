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

"""Reusable kinematics, Cartesian impedance, and motor-command helpers."""

from dataclasses import dataclass, fields, replace
import threading
import time

import numpy as np
import pinocchio as pin

from rebot_robot import JointState, MotorCommand, number, require


@dataclass(frozen=True)
class CartesianParams:
    end_effector_frame: str = "end_link"
    stiffness: float = 300.0
    damping: float = 12.0
    joint_damping: float = 1.0
    max_force: float = 15.0
    max_error: float = 0.10
    max_joint_speed: float = 4.0
    max_cartesian_speed: float = 1.0
    torque_fraction: float = 0.8

    @classmethod
    def from_mapping(cls, values):
        require(isinstance(values, dict), "Cartesian configuration must be a mapping")
        require(not (values.keys() - {f.name for f in fields(cls)}), "unknown Cartesian parameter")
        defaults = cls()
        frame = values.get("end_effector_frame", defaults.end_effector_frame)
        require(isinstance(frame, str) and frame, "end_effector_frame must not be empty")
        limits = dict(stiffness=(0, 5000), damping=(0, 100), joint_damping=(0, 5),
                      max_force=(0.1, 100), max_error=(0.001, 0.5),
                      max_joint_speed=(0.1, 8), max_cartesian_speed=(0.05, 2),
                      torque_fraction=(0.1, 1))
        return cls(end_effector_frame=frame, **{
            name: number(values.get(name, getattr(defaults, name)), name, *bounds)
            for name, bounds in limits.items()})


@dataclass(frozen=True)
class ControlSample:
    """One observation, enriched by each stage; published fields are immutable."""

    state: JointState
    gravity_torque: tuple[float, ...]
    tool_position: tuple[float, ...] = ()
    tool_velocity: tuple[float, ...] = ()
    jacobian: tuple[tuple[float, ...], ...] = ()
    task_torque: tuple[float, ...] = ()
    target: tuple[float, ...] | None = None


class HoldTarget:
    """The terminal requests a change; the control thread captures the actual point."""

    def __init__(self):
        self._lock = threading.Lock()
        self._request = None
        self._target = None

    def request(self, action):
        require(action in ("hold", "release"), "unknown hold action")
        with self._lock:
            self._request = action

    def resolve(self, position):
        with self._lock:
            if self._request == "hold":
                self._target = tuple(position)
            elif self._request == "release":
                self._target = None
            self._request = None
            return self._target

    def snapshot(self):
        with self._lock:
            return self._target


class ToolKinematics:
    """Share the immutable robot model, with a separate Pinocchio Data workspace."""

    def __init__(self, gravity, frame_name):
        self.gravity, self.frame_name = gravity, frame_name
        self.data = None

    def initialize(self):
        require(self.gravity.model is not None, "initialize the gravity model first")
        require(self.data is None, "kinematics is already initialized")
        self.model = self.gravity.model
        self.frame_id = self.model.getFrameId(self.frame_name)
        require(self.frame_id < self.model.nframes, f"unknown tool frame: {self.frame_name}")
        self.data = self.model.createData()
        self.q = pin.neutral(self.model)

    def compute(self, sample, *, joint_direction, max_joint_speed, max_cartesian_speed):
        require(self.data is not None, "kinematics has not been initialized")
        state = sample.state
        indices = self.gravity.arm_indices
        require(len(state.velocity) == len(self.gravity.config.joints), "joint velocity size mismatch")
        velocity = np.asarray(state.velocity)[indices]
        require(np.all(np.isfinite(velocity)), "nonfinite joint velocity")
        require(np.all(np.abs(velocity) <= max_joint_speed), "joint speed exceeds Cartesian limit")
        direction = np.asarray(joint_direction)
        self.q[self.gravity.q_indices] = np.asarray(state.position)[indices] * direction
        pin.computeJointJacobians(self.model, self.data, self.q)
        pin.updateFramePlacements(self.model, self.data)
        # The point is at end_link's origin, expressed in fixed URDF base axes.
        jacobian = pin.getFrameJacobian(self.model, self.data, self.frame_id,
                                       pin.LOCAL_WORLD_ALIGNED)[:3, self.gravity.v_indices]
        jacobian = jacobian * direction  # Derivatives with respect to motor angles.
        position = self.data.oMf[self.frame_id].translation
        speed = jacobian @ velocity
        require(np.all(np.isfinite(position)) and np.all(np.isfinite(jacobian)), "nonfinite kinematics")
        require(np.all(np.isfinite(speed)) and np.linalg.norm(speed) <= max_cartesian_speed,
                "tool speed exceeds Cartesian limit")
        return replace(sample, tool_position=tuple(float(x) for x in position),
                       tool_velocity=tuple(float(x) for x in speed),
                       jacobian=tuple(tuple(float(x) for x in row) for row in jacobian))


def cartesian_impedance(sample, target, *, stiffness, damping, max_force, max_error):
    """A Cartesian spring/damper mapped into motor-coordinate joint torques."""
    jacobian = np.asarray(sample.jacobian)
    require(jacobian.shape == (3, 6) and np.all(np.isfinite(jacobian)), "invalid Cartesian Jacobian")
    force = np.zeros(3)
    if target is not None:
        error = np.asarray(target) - np.asarray(sample.tool_position)
        require(error.shape == (3,) and np.all(np.isfinite(error)), "invalid target point")
        require(np.linalg.norm(error) <= max_error, "Cartesian tracking error exceeds limit")
        force = stiffness * error - damping * np.asarray(sample.tool_velocity)
        require(np.all(np.isfinite(force)), "nonfinite Cartesian force")
        norm = np.linalg.norm(force)
        if norm > max_force:
            force *= max_force / norm
    torque = jacobian.T @ force
    return replace(sample, task_torque=tuple(float(x) for x in torque), target=target)


class CartesianMotorCommand:
    """Sink-side command packing and combined feedforward/damping torque checks."""

    def __init__(self, gravity):
        self.gravity = gravity

    def compute(self, sample, *, joint_damping, torque_fraction):
        state, config = sample.state, self.gravity.config
        indices = self.gravity.arm_indices
        require(len(sample.gravity_torque) == len(config.joints), "gravity torque size mismatch")
        g = np.asarray(sample.gravity_torque)[indices]
        task = np.asarray(sample.task_torque)
        require(task.shape == (6,) and np.all(np.isfinite(task)), "invalid Cartesian joint torque")
        require(np.all(np.isfinite(g)), "nonfinite gravity torque")
        limit = self.gravity.effort * torque_fraction
        baseline = g - joint_damping * np.asarray(state.velocity)[indices]
        require(np.all(np.isfinite(baseline)) and np.all(np.abs(g) <= limit) and
                np.all(np.abs(baseline) <= limit), "gravity or joint damping exceeds torque budget")
        # Retain gravity; reduce the complete task contribution by one common factor.
        # Bound both feedforward and estimated total torque including local damping.
        scale = 1.0
        for base in (g, baseline):
            for b, t, bound in zip(base, task, limit):
                if t > 0:
                    scale = min(scale, (bound - b) / t)
                elif t < 0:
                    scale = min(scale, (-bound - b) / t)
        scale = max(0.0, min(1.0, scale))
        torque = list(sample.gravity_torque)
        kp, kd = [j.kp for j in config.joints], [j.kd for j in config.joints]
        for a, i in enumerate(indices):
            torque[i] = float(g[a] + scale * task[a])
            kp[i], kd[i] = 0.0, joint_damping
        return MotorCommand(state.sequence, state.timestamp_ns, state.position,
                            (0.0,) * len(config.joints), tuple(kp), tuple(kd), tuple(torque))


def prepare_cartesian_hold(robot, gravity, kinematics, commands, parameters):
    """Compile first; prepare a gravity-only initial command before graph execution."""
    gravity.initialize()
    kinematics.initialize()  # Reject model/frame errors before opening the device.
    robot.connect()
    state = robot.read_state(sequence=0)
    torque = gravity.gravity_torques(state, tau_scale=gravity.parameters.tau_scale,
                                    joint_direction=gravity.parameters.joint_direction)
    sample = kinematics.compute(ControlSample(state, torque),
                                joint_direction=gravity.parameters.joint_direction,
                                max_joint_speed=parameters.max_joint_speed,
                                max_cartesian_speed=parameters.max_cartesian_speed)
    initial = commands.compute(replace(sample, task_torque=(0.0,) * 6),
                               joint_damping=parameters.joint_damping,
                               torque_fraction=parameters.torque_fraction)
    robot.prepare_mit(initial)
    robot.last_command_ns = time.monotonic_ns()
