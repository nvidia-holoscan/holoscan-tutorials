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

#include "rebot/operators/pinocchio_gravity.hpp"

#include <holoscan/core/execution_context.hpp>

#include <stdexcept>
#include <utility>

namespace rebot {

PinocchioGravityOp::PinocchioGravityOp(std::shared_ptr<GravityCompensation> gravity)
    : gravity_(std::move(gravity)) {
  if (!gravity_) throw std::invalid_argument("PinocchioGravityOp requires a gravity controller");
}

void PinocchioGravityOp::setup(holoscan::OperatorSpec& spec) {
  spec.input(state, "state").queue_depth(1);
  spec.output(command, "command");
}

holoscan::Contract PinocchioGravityOp::contract() const {
  holoscan::Contract contract;
  contract.trigger(holoscan::OnEach{state});
  return contract;
}

holoscan::expected<void, holoscan::Error> PinocchioGravityOp::compute(
    holoscan::ExecutionContext& context) {
  auto input = state.receive_data();
  if (!input) return holoscan::make_unexpected(std::move(input).error());
  try {
    const auto output = gravity_->compute(*input, context.params<GravityParams>(), steady_now_ns());
    return command.emit(output);
  } catch (const std::exception& error) {
    return holoscan::make_unexpected(holoscan::Error{holoscan::ErrorCode::kFailure, error.what()});
  }
}

}  // namespace rebot
