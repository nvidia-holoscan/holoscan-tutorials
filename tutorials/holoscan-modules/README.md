# Holoscan Modules Tutorials

## Purpose

Learn to create, package, and consume Holoscan Modules: reusable, versioned libraries that
extend Holoscan SDK 5.0 with C++ operators and related assets, optionally usable from Python.

## Tutorials

- **[Create a Holoscan Module](./create-a-module)** — Scaffold a standalone repository, implement
  an SDK 5 operator and compiled graph, call it from Python, test it, and produce a Debian package.
- **[Use a Holoscan Module](./use-a-module)** — Learn to declare a module dependency, install
  or build it, and import its operators in your own application or framework project.

Browse published modules in the [Modules tab](https://nvidia-holoscan.github.io/holohub/modules/).

## Requirements

- Holoscan SDK 5.0.0 or later with CUDA 13
- CMake 4.0 or later and a C++20-capable compiler for Modules with C++ operators
- Python 3.12 through 3.14 for the `holoscan-cli` commands used by these walkthroughs
- Docker, Docker Buildx, and NVIDIA Container Toolkit for the recommended container workflow

Module operators are implemented in C++ or Python. For C++ operators, the scaffold can add Python
bindings so Python applications can use them, and the packaged artifact is a Debian package.
From Holoscan CLI `5.1.0`, the bindings can also ship as a Python wheel, and
`holoscan create --language python` creates a pure-Python Module that ships as a Python wheel and
needs no C++ compiler.

The examples install `holoscan-cli` from PyPI; each tutorial pins the CLI version it uses. CLI and
SDK versions are independent: the scaffold requires SDK `5.0.0` or later. See the
[Holoscan CLI documentation](https://github.com/nvidia-holoscan/holoscan-cli)
for configuration and integration details.

Each tutorial includes concepts, commands, troubleshooting, and links to the relevant Holoscan SDK
and CLI references.
