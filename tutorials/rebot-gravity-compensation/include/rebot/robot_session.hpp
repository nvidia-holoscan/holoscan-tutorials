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

#include "rebot/robot.hpp"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

namespace rebot {

// One device connection shared by the source, sink, and the application's
// watchdog. Holoscan copies the shared_ptr when it constructs runtime operators.
struct RobotSession {
  explicit RobotSession(RobotConfig config, std::string motor_library = "");
  ~RobotSession();
  void disable() noexcept;

  const RobotConfig config;
  Robot robot;
  std::mutex io_mutex;
  std::atomic<std::uint64_t> commands{0};
  std::atomic<std::uint64_t> last_command_ns{0};
};

}  // namespace rebot
