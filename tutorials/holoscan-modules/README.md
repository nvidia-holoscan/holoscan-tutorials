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
- CMake 4.0 or later and a C++20-capable compiler
- Python 3.12 through 3.14 for the exact `holoscan-cli==5.0.0rc2` preview commands used by this walkthrough
- Docker, Docker Buildx, and NVIDIA Container Toolkit for the recommended container workflow

SDK 5.0 Module operators are implemented in C++. The scaffold can add Python bindings so Python
applications can use them, but it does not generate Python-implemented operators or wheel packages.
The packaged artifact is a Debian package.

The examples use the CLI preview `5.0.0rc2` from NVIDIA's Python index because that is the
version pinned by the current SDK 5 template. CLI and SDK versions are independent: the scaffold
uses CLI `5.0.0rc2` while requiring SDK `5.0.0` or later. See the
[Holoscan CLI documentation](https://github.com/nvidia-holoscan/holoscan-cli)
for configuration and integration details.

Each tutorial includes concepts, commands, troubleshooting, and links to the relevant Holoscan SDK
and CLI references.
