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

#include <motor_abi.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <vector>

struct MotorController {};
struct MotorHandle {
  MotorState cache{};
  std::uint8_t status{};
  std::uint32_t watchdog{};
};

namespace {
std::vector<MotorHandle*> motors;
std::atomic<bool> frozen{false}, fail_send{false};
std::atomic<int> enables{0}, disables{0}, writes{0}, mit_writes{0};
std::atomic<float> first_kp{0};
void update(MotorHandle* motor) {
  if (frozen) return;
  motor->cache.has_value = 1;
  motor->cache.status_code = motor->status;
}
}

extern "C" {
void fake_reset() { frozen = false; fail_send = false; enables = disables = writes = mit_writes = 0; first_kp = 0; }
void fake_freeze(int value) { frozen = value; }
void fake_send_error(int value) { fail_send = value; }
int fake_enables() { return enables; }
int fake_disables() { return disables; }
int fake_writes() { return writes; }
int fake_mit_writes() { return mit_writes; }
float fake_first_kp() { return first_kp; }
void fake_set_status(int index, int status) { motors.at(index)->status = status; update(motors.at(index)); }
const char* motor_last_error_message() { return "injected motorbridge failure"; }
MotorController* motor_controller_new_dm_serial(const char*, uint32_t) { return new MotorController; }
void motor_controller_free(MotorController* controller) { delete controller; }
int32_t motor_controller_poll_feedback_once(MotorController*) { return 0; }
int32_t motor_controller_close_bus(MotorController*) { return 0; }
MotorHandle* motor_controller_add_damiao_motor(MotorController*, uint16_t id, uint16_t feedback, const char*) {
  auto* motor = new MotorHandle;
  motor->cache.can_id = id; motor->cache.arbitration_id = feedback;
  motors.push_back(motor);
  return motor;
}
void motor_handle_free(MotorHandle* motor) { std::erase(motors, motor); delete motor; }
int32_t motor_handle_enable(MotorHandle* motor) {
  motor->status = 1; ++enables; ++writes; update(motor); return 0;
}
int32_t motor_handle_disable(MotorHandle* motor) {
  motor->status = 0; ++disables; ++writes; update(motor); return 0;
}
int32_t motor_handle_ensure_mode(MotorHandle*, uint32_t, uint32_t) { ++writes; return 0; }
int32_t motor_handle_set_can_timeout_ms(MotorHandle* motor, uint32_t ms) {
  motor->watchdog = ms * 20; ++writes; return 0;
}
int32_t motor_handle_get_register_u32(MotorHandle* motor, uint8_t, uint32_t, uint32_t* out) {
  *out = motor->watchdog; return 0;
}
int32_t motor_handle_request_feedback(MotorHandle* motor) { update(motor); return 0; }
int32_t motor_handle_get_state(MotorHandle* motor, MotorState* out) { *out = motor->cache; return 0; }
int32_t motor_handle_send_mit(MotorHandle* motor, float, float, float kp, float, float) {
  ++writes; ++mit_writes;
  if (fail_send) return -1;
  if (motor->cache.can_id == 1) first_kp = kp;
  update(motor); return 0;
}

// The official Python SDK binds the whole ABI when loading the library.
// Operations outside this demo fail without touching hardware.
const char* motor_abi_version(void) { return "0.5.0"; }
const char* motor_abi_capabilities_json(void) { return "{}"; }
MotorController* motor_controller_new_socketcan(const char*) { return nullptr; }
MotorController* motor_controller_new_socketcanfd(const char*) { return nullptr; }
MotorController* motor_controller_new_dm_device(const char*, const char*) { return nullptr; }
int32_t motor_controller_enable_all(MotorController*) { return -1; }
int32_t motor_controller_disable_all(MotorController*) { return -1; }
int32_t motor_controller_shutdown(MotorController*) { return -1; }
MotorHandle* motor_controller_add_hexfellow_motor(MotorController*, uint16_t, uint16_t, const char*) { return nullptr; }
MotorHandle* motor_controller_add_myactuator_motor(MotorController*, uint16_t, uint16_t, const char*) { return nullptr; }
MotorHandle* motor_controller_add_robstride_motor(MotorController*, uint16_t, uint16_t, const char*) { return nullptr; }
MotorHandle* motor_controller_add_hightorque_motor(MotorController*, uint16_t, uint16_t, const char*) { return nullptr; }
int32_t motor_handle_clear_error(MotorHandle*) { return -1; }
int32_t motor_handle_set_zero_position(MotorHandle*) { return -1; }
int32_t motor_handle_send_pos_vel(MotorHandle*, float, float) { return -1; }
int32_t motor_handle_robstride_send_pos_vel_pp(MotorHandle*, float, float, float) { return -1; }
int32_t motor_handle_robstride_send_pos_vel_csp(MotorHandle*, float, float) { return -1; }
int32_t motor_handle_send_vel(MotorHandle*, float) { return -1; }
int32_t motor_handle_send_force_pos(MotorHandle*, float, float, float) { return -1; }
int32_t motor_handle_store_parameters(MotorHandle*) { return -1; }
int32_t motor_handle_write_register_f32(MotorHandle*, uint8_t, float) { return -1; }
int32_t motor_handle_write_register_u32(MotorHandle*, uint8_t, uint32_t) { return -1; }
int32_t motor_handle_get_register_f32(MotorHandle*, uint8_t, uint32_t, float*) { return -1; }
int32_t motor_handle_robstride_ping(MotorHandle*, uint8_t*, uint8_t*) { return -1; }
int32_t motor_handle_robstride_ping_host_id(MotorHandle*, uint16_t, uint32_t, uint8_t*, uint8_t*) { return -1; }
int32_t motor_handle_robstride_get_param_f32_host_id(MotorHandle*, uint16_t, uint16_t, uint32_t, float*) { return -1; }
int32_t motor_handle_robstride_get_fault_report(MotorHandle*, uint32_t*, uint32_t*) { return -1; }
int32_t motor_handle_robstride_set_device_id(MotorHandle*, uint8_t) { return -1; }
int32_t motor_handle_robstride_set_active_report(MotorHandle*, uint8_t) { return -1; }
int32_t motor_handle_robstride_write_param_i8(MotorHandle*, uint16_t, int8_t) { return -1; }
int32_t motor_handle_robstride_write_param_u8(MotorHandle*, uint16_t, uint8_t) { return -1; }
int32_t motor_handle_robstride_write_param_u16(MotorHandle*, uint16_t, uint16_t) { return -1; }
int32_t motor_handle_robstride_write_param_u32(MotorHandle*, uint16_t, uint32_t) { return -1; }
int32_t motor_handle_robstride_write_param_f32(MotorHandle*, uint16_t, float) { return -1; }
int32_t motor_handle_robstride_get_param_i8(MotorHandle*, uint16_t, uint32_t, int8_t*) { return -1; }
int32_t motor_handle_robstride_get_param_u8(MotorHandle*, uint16_t, uint32_t, uint8_t*) { return -1; }
int32_t motor_handle_robstride_get_param_u16(MotorHandle*, uint16_t, uint32_t, uint16_t*) { return -1; }
int32_t motor_handle_robstride_get_param_u32(MotorHandle*, uint16_t, uint32_t, uint32_t*) { return -1; }
int32_t motor_handle_robstride_get_param_f32(MotorHandle*, uint16_t, uint32_t, float*) { return -1; }
int32_t motor_handle_damiao_get_param_f32(MotorHandle*, uint16_t, uint32_t, float*) { return -1; }
int32_t motor_handle_damiao_get_param_u32(MotorHandle*, uint16_t, uint32_t, uint32_t*) { return -1; }
int32_t motor_handle_damiao_write_param_f32(MotorHandle*, uint16_t, float) { return -1; }
int32_t motor_handle_damiao_write_param_u32(MotorHandle*, uint16_t, uint32_t) { return -1; }
}
