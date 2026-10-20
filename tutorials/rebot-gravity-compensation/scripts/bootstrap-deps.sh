#!/usr/bin/env bash
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

# Install the native Pinocchio dependencies into this checkout's .venv.
set -euo pipefail

rebot_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
rebot_python="${REBOT_PYTHON:-python3.12}"
rebot_env="$rebot_root/.venv"

if command -v uv >/dev/null 2>&1; then
  if [[ ! -x "$rebot_env/bin/python" ]]; then
    uv venv --python "$rebot_python" --no-python-downloads "$rebot_env"
  fi
  uv pip install --python "$rebot_env/bin/python" --only-binary=:all: \
    --requirement "$rebot_root/requirements-build.txt"
else
  if [[ ! -x "$rebot_env/bin/python" ]]; then
    "$rebot_python" -m venv "$rebot_env"
  fi
  "$rebot_env/bin/python" -m pip install --only-binary=:all: \
    --requirement "$rebot_root/requirements-build.txt"
fi

# An import checks the actual shared-library ABI, not only package metadata.
"$rebot_env/bin/python" - <<'PY'
from pathlib import Path
import sysconfig
import pinocchio

prefix = Path(sysconfig.get_path("purelib")) / "cmeel.prefix"
for config in ("lib/cmake/pinocchio/pinocchioConfig.cmake",
               "share/eigen3/cmake/Eigen3Config.cmake",
               "lib/urdfdom_headers/cmake/urdfdom_headers-config.cmake"):
    if not (prefix / config).is_file():
        raise SystemExit(f"Missing C++ dependency: {prefix / config}")
print(f"Pinocchio {pinocchio.__version__} is ready at {prefix}")
PY
