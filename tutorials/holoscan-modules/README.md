# Holoscan Modules Tutorials

## Purpose

Learn to create, package, and consume Holoscan Modules: reusable, versioned C++ libraries that
extend Holoscan SDK 5.0 with operators and related assets.

## Tutorials

- **[Create a Holoscan Module](./create-a-module)** — Scaffold a standalone repository, implement
  an SDK 5 operator and compiled graph, test it, and produce a Debian package.
- **[Use a Holoscan Module](./use-a-module)** — Learn to declare a module dependency, install
  or build it, and import its operators in your own application or framework project.

Browse published modules in the [Modules tab](https://nvidia-holoscan.github.io/holohub/modules/).

## Requirements

- Holoscan SDK 5.0.0 or later with CUDA 13
- CMake 4.0 or later and a C++20-capable compiler
- Python 3.12 through 3.14 for the exact `holoscan-cli==5.0.0rc1` preview commands used by this walkthrough
- Docker, Docker Buildx, and NVIDIA Container Toolkit for the recommended container workflow

The SDK 5.0 Module scaffold is C++-first. It does not generate Python operators, Python bindings,
editable-import hooks, or wheel packages; those return in a later release. The supported generated
artifact is a Debian package.

The examples use the released CLI preview `5.0.0rc1` from NVIDIA's Python index because that is the
version pinned by the current SDK 5 template. CLI and SDK versions are independent: the scaffold
uses CLI `5.0.0rc1` while requiring SDK `5.0.0` or later. Use `holoscan` in standalone Module
repositories and `./holohub` inside HoloHub. See the
[Holoscan CLI documentation](https://github.com/nvidia-holoscan/holohub/blob/main/utilities/cli/README.md)
for wrapper configuration and integration details.

Each tutorial includes concepts, commands, troubleshooting, and links to the relevant SDK and
HoloHub references.
