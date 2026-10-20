# Holoscan Tutorials

Step-by-step guides for learning and developing with the
[NVIDIA Holoscan SDK](https://github.com/nvidia-holoscan/holoscan-sdk). Use these
tutorials to learn a specific workflow, build an example, or apply a technique
in your own project.

This repository is part of the Holoscan ecosystem and is listed in the
[HoloHub registry](https://github.com/nvidia-holoscan/holohub). Clone and use it
directly; each tutorial provides its own walkthrough and dependencies.

## Choose a tutorial

Browse [tutorials](tutorials/) to find a topic that matches your goals. Each
walkthrough explains what you will learn and what you need to follow along.

## Getting started

Clone this repository:

```bash
git clone https://github.com/nvidia-holoscan/holoscan-tutorials.git
cd holoscan-tutorials
```

1. Read the selected tutorial's README for its goals, supported platforms,
   hardware, software, and data requirements. Its `metadata.json` also records
   the required and tested SDK versions.
2. Prepare the environment described in the tutorial. Refer to the
   [SDK installation guide](https://docs.nvidia.com/holoscan/sdk-user-guide/setup/sdk-installation)
   when an SDK installation is required; some tutorials use a container instead.
3. Follow the walkthrough from the working directories it specifies. Setup and
   execution are specific to each tutorial.

## Holoscan CLI

The [Holoscan CLI](https://github.com/nvidia-holoscan/holoscan-cli) can discover
tutorials from the repository root with `holoscan list`. Tutorials that register
a CMake application in `tutorials/CMakeLists.txt` can also use CLI build, run,
and install commands. Other tutorials provide their own workflows in their
READMEs. See the [cross-compilation example](tutorials/cross_compilation_example/README.md)
for a CLI build and install from this repository.

## Next steps and contributions

Explore [Holoscan Applications](https://github.com/nvidia-holoscan/holoscan-applications)
for complete examples and [Holoscan Benchmarks](https://github.com/nvidia-holoscan/holoscan-benchmarks)
for performance measurement and results. Use HoloHub to discover additional
projects and modules.

To add or improve a tutorial, see [CONTRIBUTING.md](CONTRIBUTING.md).

Licensed under [Apache 2.0](LICENSE).
