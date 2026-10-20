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

#include "rebot/dynamics.hpp"

#include <pinocchio/algorithm/joint-configuration.hpp>
#include <pinocchio/algorithm/rnea.hpp>
#include <pinocchio/parsers/urdf.hpp>

#include <cmath>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace rebot {
namespace {

void validate_vector(const std::vector<double>& values, std::size_t expected,
                     const char* label) {
  if (values.size() != expected) {
    throw std::invalid_argument(std::string(label) + " must contain " +
                                std::to_string(expected) + " joint values");
  }
  for (double value : values) {
    if (!std::isfinite(value)) {
      throw std::invalid_argument(std::string(label) + " contains a non-finite value");
    }
  }
}

pinocchio::Model load_model(const std::string& path,
                           const std::array<double, 3>& gravity) {
  for (double component : gravity) {
    if (!std::isfinite(component)) {
      throw std::invalid_argument("gravity contains a non-finite value");
    }
  }
  pinocchio::Model model;
  pinocchio::urdf::buildModel(path, model);
  model.gravity.linear() = Eigen::Vector3d(gravity[0], gravity[1], gravity[2]);
  model.gravity.angular().setZero();
  return model;
}

}  // namespace

struct PinocchioDynamics::Impl {
  pinocchio::Model model;
  pinocchio::Data data;
  std::vector<std::string> names;
  std::vector<Eigen::Index> q_indices;
  std::vector<Eigen::Index> v_indices;
  JointLimits limits;
  Eigen::VectorXd neutral;
  Eigen::VectorXd q;

  Impl(const std::string& path, std::vector<std::string> joint_names,
       const std::array<double, 3>& gravity)
      : model(load_model(path, gravity)),
        data(model),
        names(std::move(joint_names)),
        neutral(pinocchio::neutral(model)),
        q(neutral) {
    if (names.empty()) {
      throw std::invalid_argument("at least one controlled joint name is required");
    }
    std::unordered_set<std::string> seen;
    for (const auto& name : names) {
      if (!seen.insert(name).second) {
        throw std::invalid_argument("duplicate controlled joint: " + name);
      }
      if (!model.existJointName(name) || name == "universe") {
        throw std::invalid_argument("controlled joint is absent from URDF: " + name);
      }
      const auto joint_id = model.getJointId(name);
      const auto& joint = model.joints[joint_id];
      if (joint.nq() != 1 || joint.nv() != 1) {
        throw std::invalid_argument("controlled joint must have one position and velocity: " + name);
      }
      q_indices.push_back(joint.idx_q());
      v_indices.push_back(joint.idx_v());
      limits.lower_position.push_back(model.lowerPositionLimit[joint.idx_q()]);
      limits.upper_position.push_back(model.upperPositionLimit[joint.idx_q()]);
      limits.velocity.push_back(model.velocityLimit[joint.idx_v()]);
      limits.effort.push_back(model.effortLimit[joint.idx_v()]);
    }
  }

  void set_configuration(const std::vector<double>& position) {
    validate_vector(position, names.size(), "position");
    q = neutral;
    for (std::size_t index = 0; index < names.size(); ++index) {
      q[q_indices[index]] = position[index];
    }
  }

  std::vector<double> selected_torque(const Eigen::VectorXd& torque) const {
    std::vector<double> result;
    result.reserve(names.size());
    for (auto index : v_indices) {
      const double value = torque[index];
      if (!std::isfinite(value)) {
        throw std::runtime_error("Pinocchio produced a non-finite torque");
      }
      result.push_back(value);
    }
    return result;
  }
};

PinocchioDynamics::PinocchioDynamics(const std::string& urdf_path,
                                   std::vector<std::string> joint_names,
                                   std::array<double, 3> gravity)
    : impl_(std::make_unique<Impl>(urdf_path, std::move(joint_names), gravity)) {}

PinocchioDynamics::~PinocchioDynamics() = default;
PinocchioDynamics::PinocchioDynamics(PinocchioDynamics&&) noexcept = default;
PinocchioDynamics& PinocchioDynamics::operator=(PinocchioDynamics&&) noexcept = default;

const JointLimits& PinocchioDynamics::joint_limits() const {
  return impl_->limits;
}

std::vector<double> PinocchioDynamics::gravity(const std::vector<double>& position) {
  impl_->set_configuration(position);
  pinocchio::computeGeneralizedGravity(impl_->model, impl_->data, impl_->q);
  return impl_->selected_torque(impl_->data.g);
}

}  // namespace rebot
