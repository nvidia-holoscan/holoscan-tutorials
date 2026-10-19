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

#pragma once

#include <dlfcn.h>
#include <stdexcept>
#include <string>

// Keep this handle alive while Robot loads the same fake library, so fault
// injection and counters refer to the instance used by the real driver wrapper.
struct FakeMotor {
  void* library = dlopen(REBOT_TEST_MOTOR_LIBRARY, RTLD_NOW | RTLD_LOCAL);
  template<class T> T symbol(const char* name) {
    if (!library) throw std::runtime_error("Cannot load fake motorbridge");
    auto result = reinterpret_cast<T>(dlsym(library, name));
    if (!result) throw std::runtime_error(std::string("Missing test hook: ") + name);
    return result;
  }
  void (*reset)() = symbol<void (*)()>("fake_reset");
  void (*freeze)(int) = symbol<void (*)(int)>("fake_freeze");
  void (*fail_send)(int) = symbol<void (*)(int)>("fake_send_error");
  void (*set_status)(int, int) = symbol<void (*)(int, int)>("fake_set_status");
  int (*enables)() = symbol<int (*)()>("fake_enables");
  int (*disables)() = symbol<int (*)()>("fake_disables");
  int (*writes)() = symbol<int (*)()>("fake_writes");
  int (*mit_writes)() = symbol<int (*)()>("fake_mit_writes");
  float (*first_kp)() = symbol<float (*)()>("fake_first_kp");
  ~FakeMotor() { if (library) dlclose(library); }
};
