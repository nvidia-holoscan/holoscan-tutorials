#!/usr/bin/env python3
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

"""Fetch the Seeed dynamics model for local use; see urdf/SOURCES.md."""

import hashlib
from pathlib import Path
import subprocess
import tempfile
import xml.etree.ElementTree as ET

REPOSITORY = "https://github.com/Seeed-Projects/reBotArm_control_py.git"
REVISION = "1bcd81b22c182ec257bf04f5746e1e3d556a5f1c"
MODEL_PATH = "urdf/DM/urdf/ReBot_Arm_DM.urdf"
SOURCE_SHA256 = "cd3054edec30b70a3afb2e97eb288ad4943a31c9746f8775703733b64ac0c3cf"


def fetch_urdf(destination: Path) -> None:
    with tempfile.TemporaryDirectory(prefix="rebot-urdf-") as directory:
        checkout = Path(directory) / "upstream"
        # Read the pinned Git blob without checking out or running upstream code.
        subprocess.run(
            ["git", "clone", "--filter=blob:none", "--no-checkout", REPOSITORY, str(checkout)],
            check=True,
        )
        source = subprocess.run(
            ["git", "-C", str(checkout), "show", f"{REVISION}:{MODEL_PATH}"],
            check=True, stdout=subprocess.PIPE,
        ).stdout
    digest = hashlib.sha256(source).hexdigest()
    if digest != SOURCE_SHA256:
        raise RuntimeError(f"URDF checksum mismatch: expected {SOURCE_SHA256}, got {digest}")

    model = ET.fromstring(source)
    for parent in model.iter():
        for child in list(parent):
            if child.tag in {"visual", "collision", "material"}:
                parent.remove(child)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(ET.tostring(model, encoding="utf-8", xml_declaration=True) + b"\n")
    print(f"Verified {REVISION}; wrote dynamics model to {destination}")


if __name__ == "__main__":
    fetch_urdf(Path(__file__).resolve().parents[1] / MODEL_PATH)
