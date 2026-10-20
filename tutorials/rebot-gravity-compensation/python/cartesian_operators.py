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

"""Five reusable Holoscan operator factories for Cartesian point hold.

Closures bind prepared resources; hsdk.Param holds only configuration values.
Each factory returns an ordinary Operator class that another graph can instantiate.
"""

import holoscan as hsdk

from rebot_cartesian import ControlSample, cartesian_impedance


def joint_state_source(robot):
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

    return JointStateSourceOp


def gravity_torque(gravity):
    class GravityTorqueOp(hsdk.Operator):
        state = hsdk.Input("state", hsdk.pyobject)
        sample = hsdk.Output("sample", hsdk.pyobject)
        tau_scale = hsdk.Param([1.0] * 6)
        joint_direction = hsdk.Param([1.0] * 6)

        def setup(self, spec):
            spec.input(self.state, queue_depth=1)
            spec.output(self.sample)

        def contract(self):
            return hsdk.Contract().trigger(hsdk.OnEach(self.state))

        def compute(self, ctx):
            received = self.state.receive()
            torque = gravity.gravity_torques(received.data, tau_scale=self.tau_scale,
                                            joint_direction=self.joint_direction)
            self.sample.forward(received, ControlSample(received.data, torque))

    return GravityTorqueOp


def pinocchio_kinematics(kinematics):
    class PinocchioKinematicsOp(hsdk.Operator):
        sample = hsdk.Input("sample", hsdk.pyobject)
        kinematics = hsdk.Output("kinematics", hsdk.pyobject)
        joint_direction = hsdk.Param([1.0] * 6)
        max_joint_speed = hsdk.Param(4.0, range=(0.1, 8.0))
        max_cartesian_speed = hsdk.Param(1.0, range=(0.05, 2.0))

        def setup(self, spec):
            spec.input(self.sample, queue_depth=1)
            spec.output(self.kinematics)

        def contract(self):
            return hsdk.Contract().trigger(hsdk.OnEach(self.sample))

        def compute(self, ctx):
            received = self.sample.receive()
            result = kinematics.compute(received.data, joint_direction=self.joint_direction,
                                        max_joint_speed=self.max_joint_speed,
                                        max_cartesian_speed=self.max_cartesian_speed)
            self.kinematics.forward(received, result)

    return PinocchioKinematicsOp


def cartesian_controller(target):
    class CartesianImpedanceOp(hsdk.Operator):
        kinematics = hsdk.Input("kinematics", hsdk.pyobject)
        effort = hsdk.Output("effort", hsdk.pyobject)
        stiffness = hsdk.Param(300.0, range=(0.0, 5000.0))
        damping = hsdk.Param(12.0, range=(0.0, 100.0))
        max_force = hsdk.Param(15.0, range=(0.1, 100.0))
        max_error = hsdk.Param(0.10, range=(0.001, 0.5))

        def setup(self, spec):
            spec.input(self.kinematics, queue_depth=1)
            spec.output(self.effort)

        def contract(self):
            return hsdk.Contract().trigger(hsdk.OnEach(self.kinematics))

        def compute(self, ctx):
            received = self.kinematics.receive()
            point = target.resolve(received.data.tool_position)
            result = cartesian_impedance(received.data, point, stiffness=self.stiffness,
                                         damping=self.damping, max_force=self.max_force,
                                         max_error=self.max_error)
            self.effort.forward(received, result)

    return CartesianImpedanceOp


def motor_command_sink(robot, commands):
    class MotorCommandSinkOp(hsdk.Operator):
        effort = hsdk.Input("effort", hsdk.pyobject)
        joint_damping = hsdk.Param(1.0, range=(0.0, 5.0))
        torque_fraction = hsdk.Param(0.8, range=(0.1, 1.0))

        def setup(self, spec):
            spec.input(self.effort, queue_depth=1)

        def contract(self):
            return hsdk.Contract().trigger(hsdk.OnEach(self.effort))

        def compute(self, ctx):
            command = commands.compute(self.effort.receive_data(), joint_damping=self.joint_damping,
                                       torque_fraction=self.torque_fraction)
            robot.send_command(command)

        def stop(self):
            robot.disable()

    return MotorCommandSinkOp
