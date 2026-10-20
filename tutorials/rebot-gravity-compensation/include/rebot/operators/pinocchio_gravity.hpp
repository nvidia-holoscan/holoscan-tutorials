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

#include "rebot/gravity_compensation.hpp"

#include <holoscan/core/operator.hpp>
#include <holoscan/core/operator_spec.hpp>
#include <holoscan/core/port.hpp>

#include <memory>

namespace rebot {

class PinocchioGravityOp final : public holoscan::Operator<GravityParams> {
 public:
  explicit PinocchioGravityOp(std::shared_ptr<GravityCompensation> gravity);
  void setup(holoscan::OperatorSpec& spec) override;
  holoscan::Contract contract() const override;
  holoscan::expected<void, holoscan::Error> compute(holoscan::ExecutionContext&) override;

  holoscan::Input<messages::JointStateT> state;
  holoscan::Output<messages::GravityCommandT> command;

 private:
  std::shared_ptr<GravityCompensation> gravity_;
};

}  // namespace rebot
