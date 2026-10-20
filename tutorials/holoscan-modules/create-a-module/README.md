# Tutorial: Create a Holoscan Module

In this tutorial, we'll create a standalone **Holoscan Module** for Holoscan SDK 5.0. We'll
start with an empty directory, build a reusable C++ operator, connect it to a finite example graph,
optionally call it from Python, exercise it with deterministic tests, and finish with a Debian
package that another Holoscan project can install.

By the end, you'll have an independently versioned repository and a repeatable development
workflow.

## 1. Holoscan Modules in Context

### What is NVIDIA Holoscan?

Holoscan SDK is NVIDIA's platform for building streaming, low-latency AI pipelines for medical
devices, robotics, and industrial edge systems. A Holoscan application connects discrete
processing units, called *operators*, into a graph that the SDK compiles and executes.

### What is a Holoscan Module?

A **Holoscan Module** is a redistributable project library that extends `holoscan::core` with a related
set of operators, resources, and helpers. For example, a camera vendor might collect its capture
and control operators in `holoscan-my-camera`, while an imaging team might publish reconstruction
operators as `holoscan-image-reconstruction`.

A Module is a good fit when the implementation should be reused across applications, maintained
on its own release schedule, or installed as a binary dependency. An ordinary application that
is not a reusable library does not need to become a Module.

This tutorial creates a **standalone Module**: an independent `holoscan-<name>` repository with
its own build, tests, package, and CI.

In Holoscan SDK 5.0, Module operators are implemented in C++. The scaffold can also add Python bindings: a
small extension that registers the C++ operators with the SDK operator registry so Python
applications can use them. It does not generate Python-implemented operators or wheels.

### Who creates a Holoscan Module?

You may want to create one when:

- you are a hardware vendor making a sensor, camera, or accelerator easy to use from Holoscan;
- you have domain-specific processing operators that several applications can share;
- an operator developed in a sample has matured into a separately maintained component; or
- you need a reproducible Debian package tied to a known Holoscan SDK version.

In each case, the goal is the same: give consumers a stable CMake target and APT installation
experience without asking them to copy source files or understand your repository internals.

### Why use the Module scaffold?

Starting from a blank C++ project leaves you to design the metadata, CMake exports, development
container, tests, packaging, and CI conventions yourself. `holoscan create` gives you a coherent
starting point where those pieces already agree on names and SDK requirements. You can spend your
time on the operator while still retaining full control of the generated repository.

## 2. Tutorial Prerequisites

Over the next sections, we'll create a dedicated repository and work with it either in its
generated development container or directly on a compatible host. The container path is
recommended because it keeps the SDK, CUDA, compiler, and system dependencies together.

### Container approach (recommended)

Install:

- Python 3.12, 3.13, or 3.14 for the exact `holoscan-cli` commands in this tutorial
- Python virtual-environment support (`python3.12-venv` on Ubuntu 24.04) if using the pip path
- Docker with the Buildx plugin
- NVIDIA Container Toolkit 1.14 or later
- (Optional) [uv](https://docs.astral.sh/uv/getting-started/installation/), or a Python virtual environment

CLI interpreter support does not establish SDK, CUDA, or Module compatibility; validate those
separately.

### Local approach

A native build is useful when you already maintain a compatible SDK toolchain on the host. In
addition to Python and uv, install:

- Holoscan SDK 5.0.0 or later (`apt install holoscan`)
- CUDA Toolkit 13.0 or later, matching Holoscan SDK 5
- CMake 4.0 or later
- GCC 13 or another a C++20-capable compiler
- GoogleTest and `clang-format`

For the optional Python bindings, also install Python 3.12 or later with development headers,
pytest, and the Holoscan SDK Python package for the same SDK version, importable as `holoscan`.
The SDK Debian packages don't include the Python package; the development container does. CMake
downloads nanobind 3.0.1 while configuring when it isn't installed, which needs network access.

The CLI version does not select the SDK version. This tutorial pins Holoscan CLI `5.0.0` and
explicitly selects Holoscan SDK `5.0.0`.

## 3. Create an External Module

We'll use the packaged template to create `holoscan-my-module`, a fictional standalone Module
repository. It can live on GitHub, GitLab, or another source host; consumers only need its
published package or immutable source location.

### 3.1 Preview and generate the project

Start in the directory that will contain the new repository. Pass the friendly, unprefixed name
`my-module`; the CLI will derive `holoscan-my-module` and the related C++ identifiers.

**Quick Start**: Start in a new parent directory and use an isolated Python environment for
interactive setup:

```bash
python3.12 -m venv .cli-venv
source .cli-venv/bin/activate
python -m pip install 'holoscan-cli[create]==5.0.0'
holoscan create my-module    # simplified command for interactive setup
```

**(Alternative) Non-interactive Setup with `uv`**: Before we
write anything to disk, let's preview exactly what the CLI plans to create.

With `uv`:

```bash
uvx --python 3.12 --from 'holoscan-cli[create]==5.0.0' \
  holoscan create my-module \
  --language cpp \
  --interactive false \
  --context holoscan_version=5.0.0 \
  --dryrun

uvx --python 3.12 --from 'holoscan-cli[create]==5.0.0' \
  holoscan create my-module \
  --language cpp \
  --interactive false \
  --context holoscan_version=5.0.0

cd holoscan-my-module
```

The first command creates nothing. Once its output looks right, the second command validates the
project in a staging area and then materializes it. Use `--directory /path/to/parent` to choose a
different parent directory. The CLI accepts a missing, empty, or `.git`-only destination and
preserves existing Git state; it refuses to overwrite a populated directory.

We used `--interactive false` so the example is reproducible. For your own Module, add repeated
`--context key=value` arguments for release metadata:
`project_name`, `module_slug`, `module_repo_name`, `operator_slug`, `full_name`, `affiliation`,
`version`, `description`, `_license`, and `contact_email`.

These names appear in several build and packaging systems, so keep them consistent:

| Context | Example |
| --- | --- |
| Display name | `My Module` |
| Module/C++ namespace | `my_module` / `holoscan::my_module` |
| Repository and Debian package | `holoscan-my-module` |
| Initial operator slug/class | `my_module_op` / `MyModuleOp` |

Take a moment to review the values before moving on; renaming a repository, package, namespace,
and operator together is much easier now than after consumers depend on them.

To also use the operators from Python, pass `--language cpp python` instead. This adds Python
bindings for the C++ operator, a Python version of the example, and a `pytest` suite;
[section 4.5](#45-call-the-operator-from-python) walks through them.

Note that `--language python` on its
own is rejected because Holoscan SDK 5.0 does not yet support Python-implemented operators.

### 3.2 Set up the generated development environment

Now that the repository exists, let's set up its development tools. The generated
`requirements-cli.txt` and `pyproject.toml` both pin the CLI used to create the project, keeping
host and container commands on the same version. With `uv`:

```bash
uv run holoscan version --json
uv run holoscan list --json
```

The generated `pyproject.toml` selects NVIDIA's Python index for the CLI; the pip fallback uses
the indexes in `requirements-cli.txt`. `pyproject.toml` has
`package = false`: uv is provisioning the development environment, not building a Python version
of your Module.

If `uv` is not available, create the same isolated environment with `pip`:

```bash
python3.12 -m venv .venv
source .venv/bin/activate
python -m pip install -r requirements-cli.txt
holoscan version --json
holoscan list --json
```

The parent `.cli-venv` is only for project generation. From the generated Module root, use
`uv run` for later commands, or reactivate that Module's `.venv` if you used pip.

### 3.3 Inspect the generated tree

Let's take a tour of what the CLI created. The scaffold is deliberately complete enough to build,
test, and package before you add project-specific infrastructure:

```text
holoscan-my-module/
├── metadata.json                 # Module identity, SDK floor, C++ namespace, DEB package
├── pyproject.toml                # CLI/ruff development tooling; not a wheel build
├── requirements-cli.txt          # Exact CLI pin for host and container
├── CMakeLists.txt                # CMake 4.0, C++20, Holoscan SDK 5.0
├── Dockerfile                    # SDK 5.0 CUDA 13 development image
├── README.md
├── DEVELOPER.md
├── cmake/                        # CMake helpers bundled with the Holoscan CLI
├── operators/
│   └── my_module_op/
│       ├── metadata.json
│       ├── CMakeLists.txt
│       ├── my_module_op.hpp      # Typed Input/Output members
│       └── my_module_op.cpp      # expected-returning compute implementation
├── applications/
│   └── my_module_pipeline/
│       ├── cpp/                  # Graph → compile → run_async example
│       └── python/               # Python example (with --language cpp python)
├── python/                       # Python bindings (with --language cpp python)
├── tests/
│   ├── cpp/                      # Compile-plan and SyntheticClock GTests
│   └── python/                   # pytest suite (with --language cpp python)
├── pkg/
│   └── holoscan-my-module/       # CPack Debian configuration
└── .github/workflows/ci.yml
```

Key things to notice:

- `metadata.json` is the shared identity for discovery, dependency resolution, and packaging.
- the top-level CMake project builds the operator, example application, tests, and DEB package;
- the Dockerfile captures the Holoscan SDK 5.0 and CUDA 13 development environment; and
- the example and tests consume the same public operator surface that another project will use.

Before publishing, update `metadata.json`, especially `source_repository`, authors, license,
operator names, supported platforms, and `holoscan_sdk.minimum_required_version`. The generated
metadata declares a C++ namespace and a Debian binary package; with Python bindings it also lists
`Python` and the `holoscan_my_module` Python namespace.

## 4. Implement, Build, and Test

The scaffold gives us a working pass-through example. Next we'll turn that example into the
starting point for a real Module, connect it to a graph, and use the generated tests to shorten
the edit-build-debug loop.

### 4.1 Implement a typed SDK 5 operator

Let's begin with the reusable part: the operator. Holoscan SDK operators declare typed ports as members,
bind them during `setup()`, and return errors from `compute()` through `expected`. The generated
pass-through operator follows this shape:

```cpp
// my_module_op.hpp
#pragma once

#include <cstdint>
#include <holoscan/core/errors.hpp>
#include <holoscan/core/execution_context.hpp>
#include <holoscan/core/expected.hpp>
#include <holoscan/core/operator.hpp>
#include <holoscan/core/operator_spec.hpp>
#include <holoscan/core/port.hpp>

namespace holoscan::my_module {

class MyModuleOp final : public holoscan::Operator<> {
 public:
  holoscan::Input<std::uint32_t> in;
  holoscan::Output<std::uint32_t> out;

  void setup(holoscan::OperatorSpec& spec) override;
  [[nodiscard]] holoscan::expected<void, holoscan::Error> compute(
      holoscan::ExecutionContext& context) override;
};

}  // namespace holoscan::my_module
```

```cpp
// my_module_op.cpp
#include "my_module_op/my_module_op.hpp"

#include <holoscan/core/expected_macros.hpp>

namespace holoscan::my_module {

void MyModuleOp::setup(holoscan::OperatorSpec& spec) {
  spec.input(in, "in");
  spec.output(out, "out");
}

holoscan::expected<void, holoscan::Error> MyModuleOp::compute(
    holoscan::ExecutionContext&) {
  HOLOSCAN_TRY(auto value, in.receive_data());
  return out.emit(value);
}

}  // namespace holoscan::my_module
```

Replace the pass-through body with your useful processing and update the operator metadata to
describe the new behavior. If the operator needs configuration, use `Operator<ParamsStruct>` to
give those parameters a typed home rather than embedding application policy in `compute()`.

### 4.2 Compose, compile, and run a graph

With the operator defined, we can place it in a pipeline. Holoscan SDK applications first compose a
`Graph`, compile it into an `ExecutionPlan`, inspect any diagnostics, and only then run the
accepted plan:

```cpp
#include <iostream>

#include <holoscan/core/compile.hpp>
#include <holoscan/core/graph.hpp>
#include <holoscan/core/run.hpp>
#include <holoscan/time/realtime_clock.hpp>

#include <my_module_op/my_module_op.hpp>

int main() {
  holoscan::Graph graph{"my-pipeline"};
  const auto op = graph.op<holoscan::my_module::MyModuleOp>("my_module_op");
  // Add source and sink operators, then connect their typed ports:
  // graph.add_flow(source->out, op->in);
  // graph.add_flow(op->out, sink->in);
  graph.set_default_clock(graph.add_clock<holoscan::RealtimeClock>("clock"));

  const holoscan::ExecutionPlan plan = holoscan::compile(graph);
  if (!plan.ok()) {
    std::cerr << plan.json() << '\n';
    return 1;
  }
  return holoscan::run(plan);
}
```

The snippet highlights the API shape, while the generated demo is complete and finite: it connects
a source, Module operator, and sink; runs with a `RealtimeClock`; observes a fixed number of
outputs; and stops its `RunSession` cooperatively. Keep that termination pattern as you adapt the
demo—a sample that exits cleanly is much easier for users and CI to trust than one that runs
forever.

Operators with no inputs must opt into time through a `contract()` trigger such as `OnClock`.
Bind a clock and make it the graph default before compiling a graph that uses `OnClock`.

### 4.3 Build and run in the development container

Now we can try the full development loop. From the Module root, preview each effect-bearing
command with the same options you intend to run, then execute it:

```bash
holoscan build my_module_pipeline --language cpp --dryrun --verbose
holoscan build my_module_pipeline --language cpp --verbose

holoscan run my_module_pipeline --language cpp --dryrun --verbose
holoscan run my_module_pipeline --language cpp --verbose

holoscan test --dryrun --verbose
holoscan test --verbose
```

The expected result is a successful build, a finite application run, and a passing CTest suite.
`--language cpp` selects the C++ example. It matters once the Module has Python bindings, because
the example then has metadata for both languages and the CLI otherwise picks Python. Use
`holoscan modes my_module_pipeline --language cpp --json` to inspect the available modes.

When you need to inspect the container or iterate manually, open an interactive shell:

```bash
holoscan run-container --dryrun --verbose
holoscan run-container
```

Inside the development container, use `holoscan` directly. The generated Dockerfile starts from
the SDK 5.0 CUDA 13 development image and installs the pinned CLI plus C++ test and lint tools, so
the shell matches the automated workflow above.

### 4.4 Understand the tests

Passing tests should tell us more than “the code compiled.” The generated CTest suite therefore
has two layers:

1. A compile-plan test assembles a graph and asserts that `compile(graph)` accepts it.
2. A whole-graph test binds a `SyntheticClock`, starts `run_async(plan)`, advances one period at a
   time, waits for each observed result, requests stop, and checks the termination diagnostics.

As you implement the operator, replace the generated pass-through assertions with checks for its
real transformation. `SyntheticClock` keeps those checks repeatable, but advancing the clock only
releases work—it does not prove the work has finished. Keep the condition variable or an
equivalent synchronization mechanism so the test waits for each observed result.

### 4.5 Call the operator from Python

If you generated the Module with `--language cpp python`, Python applications can use the same C++
operator. You don't write nanobind bindings for each operator: `python/_my_module.cpp` registers
each one with a `HOLOSCAN_REGISTER_OPERATOR` line, and importing `holoscan_my_module` loads those
registrations.

```cpp
HOLOSCAN_REGISTER_OPERATOR(holoscan::my_module::MyModuleOp,
                           "holoscan.my_module.MyModuleOp");
```

`holoscan_my_module.operators` then provides a typed proxy for each registered operator. The
generated Python example composes, compiles, and runs a graph just like the C++ example, with
Python source and sink operators around the Module operator:

```python
import holoscan as hs
from holoscan_my_module.operators import MyModuleOp

graph = hs.Graph("my_module_pipeline", default_clock=hs.RealtimeClock)
source = graph.op(CounterSource, "source")
forward = MyModuleOp(graph, "my_module_op")
sink = graph.op(CountingSink, "sink")
graph.add_flow(source.output, forward["in"])  # `in` is a Python keyword
graph.add_flow(forward.out, sink.input)

plan = hs.compile(graph)
plan.require_no_errors()
```

Build the example as in section 4.3, then build and run the Python version. `holoscan run`
builds the selected language before launching it:

```bash
holoscan run my_module_pipeline --language python --dryrun --verbose
holoscan run my_module_pipeline --language python --verbose
```

`holoscan test` now also runs the `tests/python` pytest suite through CTest. It checks that the
operator is registered, that its proxy exposes the expected ports, and that the Python example
runs. When you add an operator, register it in `python/_my_module.cpp`, link its target to the
extension in `python/CMakeLists.txt`, and cover it in `tests/python`.

The `HOLOSCAN_MY_MODULE_BUILD_PYTHON` CMake option controls the bindings. It defaults to `ON` for a
standalone build and `OFF` when another project includes the Module or when packaging. To run the
Python example directly after the CLI run, execute this inside the development container,
adding the application build's Python directory in front of the SDK's existing `PYTHONPATH`:

```bash
PYTHONPATH=build/my_module_pipeline/python/lib:$PYTHONPATH \
  python3 applications/my_module_pipeline/python/my_module_pipeline.py
```

For a native Python build, use `build-native/my_module_pipeline/python/lib` instead.

### 4.6 Build and test natively

The container is the easiest way to keep the toolchain reproducible. If your host already has a
compatible SDK, CUDA toolkit, compiler, and CMake, you can use native mode for a faster local edit
cycle. Keep its output separate from container output:

```bash
export HOLOSCAN_CLI_BUILD_PARENT_DIR="$PWD/build-native"
holoscan build my_module_pipeline --language cpp --local --dryrun --verbose
holoscan test --local --dryrun --verbose
holoscan build my_module_pipeline --language cpp --local --verbose
holoscan test --local --verbose
unset HOLOSCAN_CLI_BUILD_PARENT_DIR
```

The build goes to `build-native/my_module_pipeline`; CLI `5.0.0` tests with a separate dashboard
build in `build-/`. Select a specific SDK with `--local-sdk-root /path/to/sdk` or
`HOLOSCAN_SDK_ROOT`. Holoscan SDK installs use architecture-qualified layouts such as `install-x86_64`
and `install-aarch64`.

If you need to work directly with CMake, the equivalent starting point is:

```bash
cmake -S . -B build -DBUILD_ALL=ON -DMY_MODULE_BUILD_TESTING=ON
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure -L unit
```

### 4.7 (Optional) Declare dependencies on other Modules

Modules become more useful when they compose cleanly. If this Module needs an operator from
another Module, declare that relationship in `metadata.json:module.dependencies[]` and pin the
external source to an immutable full commit SHA:

```json
{
  "module": {
    "name": "holoscan-my-module",
    "dependencies": [
      {
        "name": "holoscan-other",
        "source": {
          "git_url": "https://github.com/example/holoscan-other.git",
          "ref": "0123456789abcdef0123456789abcdef01234567"
        },
        "provides_operators": ["other_op"]
      }
    ]
  }
}
```

Holoscan CLI requires a full commit SHA; mutable tags and branches are rejected. A full SHA
lets a teammate or CI runner resolve the same source later. The
[Use a Module tutorial](../use-a-module/README.md) shows how to override that pin with a local
checkout during development without weakening the committed dependency.

### 4.8 Build the Debian package

Now that the operator builds, its behavior is tested, and the demo exits cleanly, we're ready to
package it for another machine. Preview the packaging operation first, just as we did for the
development commands:

```bash
holoscan package holoscan-my-module --pkg-generator DEB --dryrun --verbose
holoscan package holoscan-my-module --pkg-generator DEB --verbose
```

The package CMake configuration uses CPack and declares `holoscan (>= 5.0.0)`. Before publishing,
inspect both its control metadata and its payload:

```bash
dpkg-deb --info holoscan-my-module_0.1.0_$(dpkg --print-architecture).deb
dpkg-deb --contents holoscan-my-module_0.1.0_$(dpkg --print-architecture).deb
```

The Debian package contains the C++ operators only. If the Module has Python bindings,
use it by appending its installation path to your local `PYTHONPATH` environment variable:

```bash
# First run the Python variant as shown in section 4.5.
cmake --install build/my_module_pipeline --component holoscan-my-module-python \
  --prefix "$PWD/build/install"
PYTHONPATH="$PWD/build/install/lib/python3.12/site-packages:$PYTHONPATH" \
  python3 -c "import holoscan_my_module; print(holoscan_my_module.__file__)"
```

Pass `--prefix` because the default `/usr/local` prefix isn't writable in the development container
and isn't on Ubuntu's Python path. Run these commands inside the development container after
building the application with Python bindings. A prefix inside `build/` stays out of Git.
Replace `python3.12` if you use a different Python version. Module projects based on Holoscan
SDK 5.0 do not fully support building Python wheels for distribution yet.

The final proof is installation in a clean consumer where the Holoscan Debian dependency is
available. Extracting the archive may be useful for inspection, but it does not prove that APT can
satisfy and install the declared dependencies. The template creates the artifact; your project's
release process is responsible for publishing it to an APT repository.

An SDK development image can contain the SDK files without a `holoscan` entry in the Debian
package database. That alone does not satisfy the generated `Depends: holoscan (>= 5.0.0)`.
Configure a repository that supplies the required SDK 5 Debian package before testing APT
installation, and check its candidate version with `apt-cache policy holoscan`.

### 4.9 Configure shared defaults

Most Modules can begin with metadata-derived defaults. When your whole team needs the same CUDA
selection, forwarded variable, Docker option, or base image, put that shared choice in
`pyproject.toml`:

```toml
[tool.holoscan]
cuda = 13
forward-env = ["MY_SENSOR_CONFIG"]
docker-run-args = ["--network=host"]

[tool.holoscan.base-images]
x86_64 = "registry.example.com/sdk-x86_64:tested"
aarch64 = "registry.example.com/sdk-aarch64:tested"
```

Store environment variable names, never secret values. Forwarding a variable does not mount files,
so mount any referenced host path separately. Use exact image references that your team has
tested, and use `--dryrun --verbose` whenever you need to see which setting came from metadata,
project configuration, the environment, or the command line.

### 4.10 Review the generated CI

The scaffold also gives you a starting CI workflow in `.github/workflows/ci.yml`. It separates
fast feedback from hardware-backed validation:

| Job | Runner | What it checks |
| --- | --- | --- |
| Lint | GitHub-hosted CPU | ruff, metadata validation, and C++ formatting |
| CMake configure | Holoscan SDK container on a CPU runner | the generated CMake graph without building or using a GPU |
| Build and test | self-hosted GPU | the C++ build and CTest suite in the SDK container |

Treat this workflow as a useful baseline, not a finished release gate. Update the runner labels
and image for your infrastructure, keep the CLI constraints synchronized, and add hardware or
end-to-end coverage for the behavior your Module promises.

### 4.11 Publish and share

Your Module is now ready to move from a development repository into a release process. Before you
share it:

1. Push the canonical repository and set `metadata.json:module.source_repository` to its URL.
2. Replace placeholder authors, license, description, and supported-platform values.
3. Publish the inspected DEB to the APT repository used by your consumers.
4. Make `binary_packages.debian` and its install command match the package users can actually
   obtain.
5. Document the supported SDK versions and the finite example that consumers can run after
   installation.

The repository may be public or private—the scaffold does not require a particular hosting model.
If you want the wider Holoscan community to discover a public Module, coordinate with HoloHub
maintainers about the appropriate catalog or pointer entry after the release artifacts are ready.

## 5. Reference Card

Once you are familiar with the workflow, use this table as a quick reminder:

| Goal | Command or location |
| --- | --- |
| Preview scaffold | `holoscan create <name> --language cpp --context holoscan_version=5.0.0 --dryrun` |
| Inspect projects | `holoscan list --json` |
| Inspect environment | `holoscan env-info --json` / `holoscan env-check --json` |
| Add Python bindings | `holoscan create <name> --language cpp python ...` |
| Build demo | `holoscan build <module>_pipeline --language cpp` |
| Run finite demo | `holoscan run <module>_pipeline --language cpp` |
| Run Python demo | `holoscan run <module>_pipeline --language python` |
| Run CTest | `holoscan test` |
| Package | `holoscan package holoscan-<name> --pkg-generator DEB` |
| Install Python package | Build the Python variant, then `cmake --install build/<module>_pipeline --component holoscan-<name>-python --prefix <dir>` |
| Use an explicit root | `holoscan --project-root /path/to/module <command>` |

## 6. Troubleshooting

### The CLI rejects `--language python` or WHEEL

SDK 5.0 Module operators are implemented in C++. Use `--language cpp python` to call them from
Python, and install the Python package with `cmake --install` as shown in section 4.8.
Python-implemented operators and wheels aren't supported by the SDK 5 template yet. Use a CLI 4.x
release for an SDK 4 Module.

### `import holoscan` fails after setting `PYTHONPATH`

The development container finds the SDK's Python package through its own `PYTHONPATH`. Add the
Module's build directory in front (`PYTHONPATH=build/my_module_pipeline/python/lib:$PYTHONPATH`) instead of
replacing the variable.

### Graph compilation fails

Print `plan.json()` and read the diagnostics before calling `run`. Compilation catches mismatched
port types, missing connections, invalid contracts, and clock requirements while the graph is
still safe to inspect.

### A source operator never executes

An operator with no input still needs a reason to run. Add an appropriate temporal contract, such
as `OnClock`, bind a clock, and make that clock the graph default before compilation.

### A `SyntheticClock` test hangs

Advance the clock one period at a time and wait for the sink to observe each expected result.
Clock advancement releases scheduled work but does not wait for it to complete. On every exit
path, request stop and wait for the `RunSession` to finish.

### CMake cannot find Holoscan

For the simplest reproducible environment, use the generated container. For a native build,
confirm that Holoscan SDK 5.0 or later is installed and select it explicitly:

```bash
holoscan build my_module_pipeline --language cpp --local --local-sdk-root /path/to/sdk --dryrun --verbose
```

After reviewing the preview, repeat it without `--dryrun`. You can also set
`HOLOSCAN_SDK_ROOT=/path/to/sdk` for the current shell.

### A command changes after a CLI update

Compare `holoscan version --json`, the installed command's `--help`, and the pins in
`requirements-cli.txt` and `pyproject.toml`. Keep the host, container, and development-group pins
synchronized when moving to a supported replacement.

### A dependency resolver rejects `ref`

Pin the dependency to a full 40-character commit SHA. Tags and branches can be convenient during
experimentation, but they are mutable and cannot reproduce a specific source tree reliably.

## 7. Next Steps

- Follow the companion [Use a Holoscan Module](../use-a-module/README.md) tutorial to consume the
  source or installed package in your Holoscan applications.
- Browse [Holoscan Applications](https://github.com/nvidia-holoscan/holoscan-applications)
  for application examples.
- [Holoscan CLI documentation](https://github.com/nvidia-holoscan/holoscan-cli)
- Refer to the [Holoscan SDK User Guide](https://docs.nvidia.com/holoscan/sdk-user-guide/introduction/overview)
  for the broader SDK programming model.
- Read the [HoloHub contributing guide](https://github.com/nvidia-holoscan/holohub/blob/main/CONTRIBUTING.md)
  before proposing a catalog entry for your published Module.

You now have the complete producer workflow: scaffold, implement, compile, test, package, inspect,
and prepare the Module for consumers. Happy Holocoding!
