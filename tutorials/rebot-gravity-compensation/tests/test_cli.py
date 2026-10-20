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

"""Exercise the actual entrypoint and signal handling using only the fake C ABI."""
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time

executable, fake_library, config = sys.argv[1:]
environment = dict(os.environ, MOTORBRIDGE_LIB=fake_library)
for arguments, expected in [(["--help"], 0), (["--simulate"], 1), (["--probe"], 1), (["--hardware"], 1)]:
    result = subprocess.run([executable, *arguments], env=environment, capture_output=True, text=True, timeout=3)
    assert result.returncode == expected, result
    assert "Usage:" in result.stdout, result.stdout

for stop_signal in (signal.SIGINT, signal.SIGTERM):
    with tempfile.TemporaryDirectory() as directory:
        log = Path(directory) / "run.log"
        with log.open("w") as output:
            process = subprocess.Popen([executable, config], env=environment, stdout=output, stderr=output)
            try:
                deadline = time.monotonic() + 5
                while "Gravity compensation active." not in log.read_text():
                    assert process.poll() is None, log.read_text()
                    assert time.monotonic() < deadline, log.read_text()
                    time.sleep(0.02)
                process.send_signal(stop_signal)
                assert process.wait(timeout=3) == 0, log.read_text()
                text = log.read_text()
                assert "Stopped after " in text and "Stopped after 0 commands." not in text, text
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait()
print("Hardware-only CLI and SIGINT/SIGTERM shutdown passed")
