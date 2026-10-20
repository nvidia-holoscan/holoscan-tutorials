# Tutorial: Use a Holoscan Module

In this tutorial you will learn how to consume a **Holoscan Module** from a downstream
project built on the Holoscan SDK. We will walk through declaring the dependency,
installing or building the Module, and importing its operators in your own code.

## 1. Holoscan Modules in Context

### What is NVIDIA Holoscan?

**Holoscan SDK** is NVIDIA's platform for building streaming, low-latency AI pipelines
targeting medical devices, robotics, and industrial edge systems. Applications in the
ecosystem are composed of *operators* — discrete processing units connected into a
directed acyclic graph (DAG) by the SDK's pipeline executor.

### What is a Holoscan Module?

A **Holoscan Module** is a distributable project library of one or more related operators, subgraphs, or
other components to extend the Holoscan SDK application programming interface (API). A module
may be packaged and published so that any Holoscan application can consume it as a binary
dependency. It is the standard mechanism for sharing reusable Holoscan processing blocks
beyond a single application or repository. Consumers get a working, namespaced import ready to use
out of the box, without needing to learn the intricate project build details.

```py
# Example Usage
from holoscan.my_sensor import MySensorOp
```

The primary form of a Module is a standalone **External Module** — a git repository with
its own operators, applications, tests, packaging, and CI. Consumers fetch its sources at
build time or install a published binary package.

In this tutorial we use a fictional `holoscan-my-sensor` external Module as the running
worked example.

### Who consumes a Holoscan Module?

A Holoscan Module is the right dependency to declare when:

- **You are building a library or end-user product** on the Holoscan SDK and need
  and want versioned, supported operators rather than vendored or copy-pasted source.
- **You are building a Holoscan sample application** that needs hardware-specific or
  domain-specific operators — for example, a robotics application that needs a vendor
  camera driver, or a medical-imaging app that needs an image-reconstruction library.
- **You are building another Holoscan Module** whose operators depend on operators
  from an upstream Module (a transitive dependency).

In each case the goal is the same: pick up a versioned set of operators by referencing
them in metadata or installing a binary package, without forking source or maintaining
build glue.

### Why use a Holoscan Module instead of copy-pasting operators or rolling your own?

1. **Versioned and supported.** The Module's maintainer owns its build, ABI, test,
   and release. You consume a pinned commit (source build) or a pinned package
   version (binary install) and get bug fixes and SDK-compatibility updates for free.
2. **Discoverable.** Curated Holoscan Modules may be listed alongside SDK-supported
   and community work on the NVIDIA Holoscan ecosystem website. Prospective users and their AI
   agents find your dependency choices by searching the Holoscan ecosystem instead of
   guessing repo names. A vendored copy puts the entire burden of discovery on you.
3. **Drop-in.** The Holoscan CLI reads Module metadata and prepares source declarations
   for your CMake build (Path C). Binary consumers get the same operators
   via standard `pip install` or `apt install` plus a single `find_package` (C++) or
   `import` (Python). No vendor-specific build glue.

A Holoscan Module dependency gives you all of the above by adopting a small set of conventions
(the metadata schema and dependency-resolver flow described below).

## 2. Tutorial Prerequisites

This tutorial covers several approaches to consume a Holoscan Module in your project. The base prerequisites apply to all paths;
each path's section below calls out anything extra it needs.

- The target Holoscan Module — its name (e.g., `holoscan-my-sensor`), its source URL
  or its published binary package name, and either a release commit SHA (source
  build) or a binary package version (install flows).
- **Holoscan SDK >= 5.0.0** matching the Module's
  `metadata.json:module.holoscan_sdk.minimum_required_version`.
- [Python >=3.12](https://www.python.org/downloads/) for Holoscan CLI.
  Binary consumers follow the Python requirements of their SDK and module wheels.
- For Path C, install the [Holoscan CLI](https://github.com/nvidia-holoscan/holoscan-cli)
  in the consuming project's development environment. The examples use `holoscan-cli==5.0.0rc3`
  from NVIDIA's Python index, as in the
  [Create a Module tutorial](../create-a-module/README.md#32-set-up-the-generated-development-environment).

### Container Approach (Recommended for the CLI Workflow)

- [Docker](https://docs.docker.com/engine/install/ubuntu/#install-using-the-repository), including the buildx plugin (`docker-buildx-plugin`)
- the [NVIDIA Container Toolkit](https://docs.nvidia.com/datacenter/cloud-native/container-toolkit/install-guide.html) (v1.14 or later)
- A local clone of the consuming Holoscan project, with its documented development container.

### Local Approach (for External C++ / Python / Source-Embedding Flows)

- **CMake >= 4.0**
- **GCC >= 13**, or another a C++20-capable toolchain for C++ and source-embedding flows.
- **`uv`**, or `pip` and a Holoscan SDK wheel installed in the same environment for the Python flow.
- Network access during CMake configure for the source-embedding flow.

## 3. Available Approaches

The [Create a Module tutorial](../create-a-module/README.md) provides a project template with a few
standard distribution mechanisms, including source distribution, Debian packaging, or (in the future) Python wheels.
Below we discuss a few recommended options for using an external Module in a downstream project.
The Holoscan Module maintainer is ultimately responsible to document and maintain the supported distribution paths for their Module.

Pick the path that matches your project, then jump to the corresponding section.

| Your project is… | Use… |
| --- | --- |
| A standalone C++ application using a module binary package | [Path A (§4)](#4-path-a--external-c-project-binary-install) |
| A standalone C++ project using module sources | [Path B (§5)](#5-path-b--source-level-embedding-in-a-fully-external-project) |
| A Holoscan sample application using Holoscan CLI dependency metadata | [Path C (§6)](#6-path-c--consume-from-a-holoscan-project-cli-workflow) |
| Another Holoscan Module declaring a transitive dependency with Holoscan CLI | [Path C (§6.7)](#67-module-to-module-transitive-dependencies) |
| A pure-Python application or notebook | [Path D (§7)](#7-path-d--external-python-project-pip-install) |


## 4. Path A — External C++ Project (Binary Install)

Standalone Holoscan projects can consume Module binaries like any other binary package. A Holoscan Module
created from template can support Debian C++ binary packaging out of the box.

### 4.1 Install the binary

Review the Module's `metadata.json:module.binary_packages` for recommended installation commands.
For example, the installation command to add to your Dockerfile or host setup script might install from a public APT repository:

```bash
# Debian / Ubuntu
apt install holoscan-my-sensor
```

### 4.2 CMake integration

In your project's `CMakeLists.txt`:

```cmake
find_package(holoscan REQUIRED COMPONENTS core)
find_package(holoscan-my-sensor REQUIRED)

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE
    holoscan::core
    holoscan::my_sensor_op
)
```

Modules are responsible for shipping a CMake package config — for example,
`/usr/lib/<triplet>/cmake/holoscan-my-sensor/holoscan-my-sensor-config.cmake` — that
exports targets under the `holoscan::` namespace. Consult the Module's repository for its
exported targets and package-consumption examples.

If the package is not found, ensure the install prefix is on `CMAKE_PREFIX_PATH`, or
that you installed via the system package manager into a default-searched prefix
(typically `/usr`). Report any issues to the appropriate module repository maintainer.

## 5. Path B — Source-Level Embedding in a Fully External Project

Projects that rely directly on CMake without Holoscan CLI tooling can use the CMake standard
[`FetchContent`](<https://cmake.org/cmake/help/latest/module/FetchContent.html>) function to fetch and build open-source Holoscan Modules
directly.

```cmake
# Pre-fill any CMake configuration options for the external module
set(<holoscan_my_sensor_OPTION_1> <value>)
...

include(FetchContent)
FetchContent_Declare(holoscan_my_sensor
    GIT_REPOSITORY https://github.com/example/holoscan-my-sensor.git
    GIT_TAG        0123456789abcdef0123456789abcdef01234567
)
FetchContent_MakeAvailable(holoscan_my_sensor)

target_link_libraries(my_app PRIVATE holoscan::my_sensor_op)
```

Building from source may require additional dependency setup and custom configuration
options. Please refer to the individual Module's guidance on requirements
and best practices. A binary installation is usually a simpler choice for fully external projects, when available.

## 6. Path C — Consume from a Holoscan Project (CLI Workflow)

As an alternative to writing CMake code yourself, the Holoscan CLI provides a simplified
interface to generate CMake `FetchContent` strings from CLI standard `metadata.json`.

If the extenral module is also aligned to Holoscan CLI, you can simply declare the dependency
in your project's metadata and offload manifest generation, discovery, and build commands
to Holoscan CLI and CMake.

This approach may be particularly useful for layered, monolithic projects based on Holoscan CLI
that have many module dependencies.

### 6.1 Select a Holoscan project

Work from the root of the repository that consumes the Module. Application examples live in
[Holoscan Applications](https://github.com/nvidia-holoscan/holoscan-applications). Or, follow
[Create a Holoscan Module](../create-a-module/README.md) to create a new Holoscan Module project
repo from a template.

Follow the consuming project's README to prepare its development environment.
Run `holoscan list` to see projects in that checkout. The examples below assume an application
named `my_app`; substitute your project's selector and a Module compatible with its SDK.

### 6.2 Declare the dependency

Edit your subproject's `metadata.json`. For an application:

```json
{
  "$schema": "urn:holoscan:application:v1",
  "application": {
    "name": "my_app",
    "dependencies": {
      "modules": [
        {
          "name": "holoscan-my-sensor",
          "source": {
            "git_url": "https://github.com/example/holoscan-my-sensor.git",
            "ref": "0123456789abcdef0123456789abcdef01234567"
          },
          "provides_operators": ["my_sensor_op"]
        }
      ]
    }
  }
}
```

This excerpt shows the dependency fields; retain the other required fields in your application's
metadata. Field reference: the `module_dependency` definition in the Holoscan CLI's
[`module.schema.json`](https://github.com/nvidia-holoscan/holoscan-cli/blob/main/src/holoscan_cli/metadata/module.schema.json).

- **`name`** (required) — Module name as published in its own
  `metadata.json:module.name`.
- **`source.{git_url, ref}`** — the Module's git URL and a pinned 40-character commit SHA.
  Holoscan CLI requires an immutable SHA. Mutable tags and branches are rejected.
- **`provides_operators`** — operators this dependency contributes. The generated manifest
  records their provider; the consuming CMake project controls which Modules are fetched and
  which operators are enabled (`OP_<name>=ON`), as shown in §6.3.
- **`version`** (optional) — informational; not enforced by the resolver today.

Those metadata details are used by the Holoscan CLI in the next section.

### 6.3 Build

The consuming project owns its CMake dependency integration. Make the CLI's bundled
[`HoloHubConfigHelpers.cmake`](https://github.com/nvidia-holoscan/holoscan-cli/blob/main/src/holoscan_cli/cmake/HoloHubConfigHelpers.cmake)
available under `cmake/`; generated Modules already include it. In the root `CMakeLists.txt`,
load the generated manifest and make the required Module available before adding the targets
or subdirectories that use it. For the dependency above:

```cmake
include(FetchContent)
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/HoloHubConfigHelpers.cmake")
include("${CMAKE_BINARY_DIR}/external_operators_manifest.cmake")

set(OP_my_sensor_op ON CACHE BOOL "Build the sensor operator" FORCE)
FetchContent_MakeAvailable(holoscan_my_sensor)
```

The helper filename and its `holohub_declare_external_module()` function are the API names
shipped by Holoscan CLI. The provider name `holoscan_my_sensor` is derived from the
Module name by replacing characters other than letters, digits, and underscores with underscores.

```bash
holoscan build my_app --dryrun --verbose
holoscan build my_app
```

**How it works:** The Holoscan CLI reads `dependencies.modules[]` from the selected project's
metadata and writes `external_operators_manifest.cmake` into its build directory before running
CMake. The manifest declares each Module's source URL and commit. The CMake integration above
fetches the required Module and enables `my_sensor_op`; link its exported target as shown in §6.5.
Projects using CMake directly can instead declare the source themselves with
[Path B](#5-path-b--source-level-embedding-in-a-fully-external-project).

### 6.4 Iterate against a local working copy

When iterating against an unreleased Module, the source override is
`HOLOSCAN_CLI_LOCAL_<SANITIZED_MODULE_NAME>`: replace non-alphanumeric runs in the
dependency's `name` with underscores, trim leading and trailing underscores, and
uppercase the result. For `holoscan-my-sensor`, use
`HOLOSCAN_CLI_LOCAL_HOLOSCAN_MY_SENSOR`.

Holoscan CLI can forward this override by name. Mount the working copy at the path
used by the override; forwarding an environment variable does not mount files:

```bash
export HOLOSCAN_CLI_LOCAL_HOLOSCAN_MY_SENSOR=/workspace/local-modules/holoscan-my-sensor
holoscan build my_app --dryrun --verbose \
  --docker-opts="-v $HOME/repos/holoscan-my-sensor:/workspace/local-modules/holoscan-my-sensor" \
  --forward-env HOLOSCAN_CLI_LOCAL_HOLOSCAN_MY_SENSOR
holoscan build my_app \
  --docker-opts="-v $HOME/repos/holoscan-my-sensor:/workspace/local-modules/holoscan-my-sensor" \
  --forward-env HOLOSCAN_CLI_LOCAL_HOLOSCAN_MY_SENSOR
```

While the override is set, the resolver uses that working tree instead of
`source.ref`. For native `--local` builds, set the variable to the host path.
Unset it to return to the pinned dependency. Standalone modules use the same
options with `holoscan build <app>` and can persist the variable name in
`tool.holoscan.forward-env`; keep machine-specific mount paths on the command line.

### 6.5 Use the operators in your code

Python application:

```python
from holoscan.core import Application
from holoscan.my_sensor import MySensorOp
from holoscan.operators import HolovizOp

class MyApp(Application):
    def compose(self):
        sensor = MySensorOp(self, name="sensor")
        viz = HolovizOp(self, name="viz")
        self.add_flow(sensor, viz, {("out", "receivers")})

if __name__ == "__main__":
    MyApp().run()
```

C++ application: include the public header the Module installs (path is documented in
its README) and link via the Module's exported targets in your application's
`CMakeLists.txt`:

```cmake
target_link_libraries(my_app PRIVATE holoscan::my_sensor_op)
```

### 6.6 Discover what's available

```bash
holoscan list   # Lists projects in this checkout
holoscan list --json
holoscan status --json
holoscan env-check --json
```

The list is a local project inventory; visit the
[Holoscan Ecosystem website](https://nvidia-holoscan.github.io/) to discover
published projects. JSON output includes `schema_version: 1`. `env-check` can
exit nonzero when dependencies are missing.

### 6.7 Module-to-module (transitive) dependencies

If you are building a Holoscan Module that itself depends on another Module, use the
`module.dependencies[]` array in your module's `metadata.json` — it follows the same
schema as the `application.dependencies.modules[]` shown in §6.2 above. Refer to the
"Create a Holoscan Module" tutorial for the full schema and SHA-pinning discipline,
and to §6.4 above for mounted-source iteration.

From a standalone module, we recommend using a `uv` environment with Python >=3.12:

```bash
uv run holoscan list --json
uv run holoscan build my_sensor_pipeline --dryrun --verbose
uv run holoscan build my_sensor_pipeline
```

For the [pip fallback](../create-a-module/README.md#32-set-up-the-generated-development-environment), activate
the module's `.venv` with `source .venv/bin/activate` and omit `uv run`.

Enable the dependency's operators in your build graph, just as in §6.3. To work
from another directory, place `--project-root /path/to/module` before `build`.
See [Create a Module](../create-a-module/README.md) for installation. For a local
build, select the SDK explicitly with `--local-sdk-root /path/to/sdk`.

## 7. Path D — External Python Project (Pip Install)

Module wheels available on PyPI can be installed like any other PyPI dependency:

```bash
pip install holoscan              # the SDK matching your CUDA variant
pip install holoscan-my-sensor    # the module
```

Use the operator like any built-in Holoscan operator:

```python
from holoscan.core import Application
from holoscan.my_sensor import MySensorOp
from holoscan.operators import HolovizOp

class MyApp(Application):
    def compose(self):
        self.add_flow(
            MySensorOp(self, name="sensor"),
            HolovizOp(self, name="viz"),
            {("out", "receivers")},
        )

if __name__ == "__main__":
    MyApp().run()
```


> **Version note:** Holoscan CLI `5.0.0` does not yet support Python wheel packaging
> with Holoscan SDK `5.0.0` sources out of the box. Use the pre-built Holoscan SDK
> wheels distributed via PyPI. Wheel authoring support are planned for a later
> Holoscan SDK release.


## 8. Reference Card

| Goal | Where or what |
| --- | --- |
| Declare a Module dep (Holoscan app) | `metadata.json:application.dependencies.modules[]` |
| Declare a transitive dep (Module → Module) | `metadata.json:module.dependencies[]` |
| External Module reference | Include `source.{git_url, ref}` (40-char SHA) and `provides_operators` |
| Local override | `HOLOSCAN_CLI_LOCAL_<SANITIZED_MODULE_NAME>=<container-path>` env var |
| Build a Holoscan project with deps | `holoscan build <app>` |
| Install binary (C++) | `apt install holoscan-<name>` |
| Install binary (Python) | `pip install holoscan-<name>` |
| Use from C++ | `find_package(holoscan-<name> REQUIRED)` + `target_link_libraries(... holoscan::<target>)` |
| Use from Python | `from holoscan.my_module import MyOp` (substitute your Module name) |
| Use CLI-declared sources (Path C) | Include the generated manifest and call `FetchContent_MakeAvailable(...)` in the consuming CMake project |
| Embed via source — fully external (Path B) | `FetchContent_Declare(...)` + `FetchContent_MakeAvailable(...)` |
| Inspect a standalone Module | `holoscan --project-root /path/to/module list --json` |
| Discover published Holoscan Modules | [HoloHub Modules catalog](https://nvidia-holoscan.github.io/holohub/modules/) |

## 9. Troubleshooting

- **`find_package(holoscan-my-sensor)` fails.** Confirm install:
  `dpkg -L holoscan-my-sensor | grep cmake`. Add the prefix to `CMAKE_PREFIX_PATH`
  if it is not in a standard location.
- **`import holoscan.my_sensor` raises `ModuleNotFoundError`.** Check that
  `pip show holoscan-my-sensor` succeeds in the same Python environment, and that the
  Holoscan SDK wheel is installed in that same env.
- **Resolver rejects a ref that is not a 40-character commit SHA.** Replace the tag or branch
  in `source.ref` with the commit SHA it resolves to.
- **`HOLOSCAN_CLI_LOCAL_*` override has no effect.** Derive the suffix from the
  dependency `name` as described in §6.4, mount the Module into the project
  container, and set the variable there to the container path. Add `--forward-env HOLOSCAN_CLI_LOCAL_<NAME>` to forward the export;
  the variable must contain the mounted container path.
- **The operator is not built even though you declared it.** Under FetchContent +
  `PROJECT_IS_TOP_LEVEL` defaults, the Module's `BUILD_ALL` is `OFF`, so only the
  operators marked with `OP_<op>=ON` get built. In either source-build path (B or C), pass
  `-DOP_<op>=ON` on the configure line or `set(...)` it before
  `FetchContent_MakeAvailable`.
- **CMake fails with "No content recorded" inside `FetchContent_MakeAvailable`.** The external operators manifest is stale. Delete
  `external_operators_manifest.cmake` from the selected project's build directory and rebuild
  with `holoscan build <app>`; the resolver regenerates it. Confirm the root `CMakeLists.txt`
  includes that manifest before calling `FetchContent_MakeAvailable`.

## 10. Next Steps

For the full [Holoscan CLI reference](https://github.com/nvidia-holoscan/holoscan-cli/blob/main/CLI_REFERENCE.md),
visit the [Holoscan CLI repository](https://github.com/nvidia-holoscan/holoscan-cli).

Happy holocoding!
