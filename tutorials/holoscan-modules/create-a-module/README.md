# Tutorial: Create a Holoscan Module

In this tutorial, we'll create a standalone C++ **Holoscan Module** for Holoscan SDK 5.0. We'll
start with an empty directory, build a reusable operator, connect it to a finite example graph,
exercise it with deterministic tests, and finish with a Debian package that another Holoscan
project can install.

By the end, you'll have an independently versioned repository and a repeatable development
workflow—not just a collection of source files.

## 1. Holoscan Modules in Context

### What is NVIDIA Holoscan?

Holoscan SDK is NVIDIA's platform for building streaming, low-latency AI pipelines for medical
devices, robotics, and industrial edge systems. A Holoscan application connects discrete
processing units, called *operators*, into a graph that the SDK compiles and executes.

### What is a Holoscan Module?

A **Holoscan Module** is a redistributable library that extends `holoscan::core` with a related
set of operators, resources, and helpers. For example, a camera vendor might collect its capture
and control operators in `holoscan-my-camera`, while an imaging team might publish reconstruction
operators as `holoscan-image-reconstruction`.

A Module is a good fit when the implementation should be reused across applications, maintained
on its own release schedule, or installed as a binary dependency. An ordinary application that
is not a reusable library does not need to become a Module.

There are two layouts:

- **Standalone Module** — an independent `holoscan-<name>` repository with its own build, tests,
  package, and CI. This is the primary workflow in this tutorial.
- **HoloHub-hosted Module** — a descriptor under HoloHub's `modules/` directory for code maintained
  in HoloHub. See [Section 5](#5-holohub-hosted-modules).

The SDK 5.0 scaffold is C++-first. It does not generate Python operators, bindings, editable-import
hooks, or wheels; those workflows are outside the scope of the current template.

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

- Python 3.12 through 3.14 for the exact `holoscan-cli==5.0.0rc1` commands in this tutorial
- [uv](https://docs.astral.sh/uv/getting-started/installation/), or a Python virtual environment
- Docker with the Buildx plugin
- NVIDIA Container Toolkit 1.12.2 or later

CLI interpreter support does not establish SDK, CUDA, or Module compatibility; validate those
separately.

### Local approach

A native build is useful when you already maintain a compatible SDK toolchain on the host. In
addition to Python and uv, install:

- Holoscan SDK 5.0.0 or later (`apt install holoscan`, the `holoscan-cuda-13` alias)
- CUDA Toolkit 13.x matching the SDK
- CMake 4.0 or later
- a C++20-capable compiler
- GoogleTest and `clang-format`

The CLI version does not select the SDK version. This tutorial uses the CLI `5.0.0rc1` preview and
explicitly selects SDK `5.0.0`.

## 3. Create an External Module

We'll use the packaged template to create `holoscan-my-module`, a fictional standalone Module
repository. It can live on GitHub, GitLab, or another source host; consumers only need its
published package or immutable source location. A HoloHub checkout is not required.

### 3.1 Preview and generate the project

Start in the directory that will contain the new repository. Pass the friendly, unprefixed name
`my-module`; the CLI will derive `holoscan-my-module` and the related C++ identifiers. Before we
write anything to disk, let's preview exactly what the CLI plans to create:

```bash
uvx --index https://pypi.nvidia.com \
  --from 'holoscan-cli[create]==5.0.0rc1' \
  holoscan create my-module \
  --language cpp \
  --interactive false \
  --context holoscan_version=5.0.0 \
  --dryrun

uvx --index https://pypi.nvidia.com \
  --from 'holoscan-cli[create]==5.0.0rc1' \
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

The SDK 5 template accepts only `--language cpp`. It rejects `--language python`; use a CLI 4.x
release when scaffolding a Module that targets the SDK 4.x API.

### 3.2 Set up the generated development environment

Now that the repository exists, let's set up its development tools. The generated
`requirements-cli.txt` and `pyproject.toml` both pin the CLI used to create the project, keeping
host and container commands on the same version. With uv:

```bash
uv sync --only-dev
source .venv/bin/activate
holoscan version --json
holoscan list --json
```

The generated project sends only the CLI development dependency to NVIDIA's Python index and
leaves other packages on PyPI. `pyproject.toml` has `package = false`: uv is provisioning the
development environment, not building a Python version of your Module.

If uv is not available, create the same isolated environment with pip:

```bash
python3.12 -m venv .venv
source .venv/bin/activate
python -m pip install \
  --extra-index-url https://pypi.nvidia.com \
  -r requirements-cli.txt
holoscan version --json
holoscan list --json
```

In later shells, reactivate `.venv` before using `holoscan`. No HoloHub checkout or copied
`./holohub` wrapper is needed in a standalone Module.

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
├── cmake/                        # HoloHub CMake helpers copied by the CLI
├── operators/
│   └── my_module_op/
│       ├── metadata.json
│       ├── CMakeLists.txt
│       ├── my_module_op.hpp      # Typed Input/Output members
│       └── my_module_op.cpp      # expected-returning compute implementation
├── applications/
│   └── my_module_pipeline/
│       └── cpp/                  # Graph → compile → run_async example
├── tests/
│   └── cpp/                      # Compile-plan and SyntheticClock GTests
├── pkg/
│   └── holoscan-my-module/       # CPack Debian configuration
└── .github/workflows/ci.yml
```

Key things to notice:

- `metadata.json` is the shared identity for discovery, dependency resolution, and packaging.
- the top-level CMake project builds the operator, example application, tests, and DEB package;
- the Dockerfile captures the SDK 5.0 and CUDA 13 development environment; and
- the example and tests consume the same public operator surface that another project will use.

Before publishing, update `metadata.json`, especially `source_repository`, authors, license,
operator names, supported platforms, and `holoscan_sdk.minimum_required_version`. The generated
metadata intentionally declares only a C++ namespace and Debian binary package.

## 4. Implement, Build, and Test

The scaffold gives us a working pass-through example. Next we'll turn that example into the
starting point for a real Module, connect it to a graph, and use the generated tests to shorten
the edit-build-debug loop.

### 4.1 Implement a typed SDK 5 operator

Let's begin with the reusable part: the operator. SDK 5 operators declare typed ports as members,
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

With the operator defined, we can place it in a pipeline. SDK 5 applications first compose a
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
holoscan build my_module_pipeline --dryrun --verbose
holoscan build my_module_pipeline --verbose

holoscan run my_module_pipeline --dryrun --verbose
holoscan run my_module_pipeline --verbose

holoscan test --dryrun --verbose
holoscan test --verbose
```

The expected result is a successful build, a finite application run, and a passing CTest suite.
The application metadata selects C++, so `--language` is not required. Use
`holoscan modes my_module_pipeline --language cpp --json` when inspecting the available mode
explicitly.

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

### 4.5 Build and test natively

The container is the easiest way to keep the toolchain reproducible. If your host already has a
compatible SDK, CUDA toolkit, compiler, and CMake, you can use native mode for a faster local edit
cycle. Keep its output separate from container output:

```bash
export HOLOSCAN_CLI_BUILD_PARENT_DIR="$PWD/build-native"
holoscan build my_module_pipeline --local --dryrun --verbose
holoscan test --local --dryrun --verbose
holoscan build my_module_pipeline --local --verbose
holoscan test --local --verbose
unset HOLOSCAN_CLI_BUILD_PARENT_DIR
```

When these commands finish, `build-native/` should contain the native artifacts while the normal
container build remains untouched. Select a specific SDK with `--local-sdk-root /path/to/sdk` or
`HOLOSCAN_SDK_ROOT`. SDK 5 installs use architecture-qualified layouts such as `install-x86_64`
and `install-aarch64`.

If you need to work directly with CMake, the equivalent starting point is:

```bash
cmake -S . -B build -DBUILD_ALL=ON -DMY_MODULE_BUILD_TESTING=ON
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure -L unit
```

### 4.6 Declare dependencies on other Modules

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

Tags and branches are accepted with a warning, but their contents can move after a successful
build. A full SHA lets a teammate or CI runner resolve the same source later. The
[Use a Module tutorial](../use-a-module/README.md) shows how to override that pin with a local
checkout during development without weakening the committed dependency.

### 4.7 Build the Debian package

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

The final proof is installation in a clean consumer where the Holoscan Debian dependency is
available. Extracting the archive may be useful for inspection, but it does not prove that APT can
satisfy and install the declared dependencies. The template creates the artifact; your project's
release process is responsible for publishing it to an APT repository.

### 4.8 Configure shared defaults

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

### 4.9 Review the generated CI

The scaffold also gives you a starting CI workflow in `.github/workflows/ci.yml`. It separates
fast feedback from hardware-backed validation:

| Job | Runner | What it checks |
| --- | --- | --- |
| Lint | GitHub-hosted CPU | ruff, metadata validation, and C++ formatting |
| CMake configure | SDK 5 container on a CPU runner | the generated CMake graph without building or using a GPU |
| Build and test | self-hosted GPU | the C++ build and CTest suite in the SDK container |

Treat this workflow as a useful baseline, not a finished release gate. Update the runner labels
and image for your infrastructure, keep the CLI constraints synchronized, and add hardware or
end-to-end coverage for the behavior your Module promises.

### 4.10 Publish and share

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

## 5. HoloHub-hosted Modules

The standalone repository we built is the right choice when a Module has its own maintainers and
release cadence. Sometimes, however, the operators and demos already live in HoloHub and should
continue to evolve with it. In that case, use an in-tree descriptor rather than copying the code
into a second repository.

Start by inspecting `modules/holoscan-gstreamer/` and the Module metadata schema. A descriptor may
add:

- `modules/<module-name>/metadata.json`
- any Module-specific `pyproject.toml` or Dockerfile that is genuinely required
- operator and demo registrations pointing to existing HoloHub projects

The descriptor groups existing HoloHub projects; it is not another copy of the standalone
scaffold. Do not restore the retired `modules/template` tree. Inside HoloHub, use `./holohub`,
preview supported mutating commands, and test each declared operator and demo explicitly. Ask the
maintainers before changing metadata schemas, shared Dockerfiles, or CMake registration macros.

## 6. Reference Card

Once you are familiar with the workflow, use this table as a quick reminder:

| Goal | Command or location |
| --- | --- |
| Preview scaffold | `holoscan create <name> --language cpp --context holoscan_version=5.0.0 --dryrun` |
| Inspect projects | `holoscan list --json` |
| Inspect environment | `holoscan env-info --json` / `holoscan env-check --json` |
| Build demo | `holoscan build <module>_pipeline` |
| Run finite demo | `holoscan run <module>_pipeline` |
| Run CTest | `holoscan test` |
| Package | `holoscan package holoscan-<name> --pkg-generator DEB` |
| Use an explicit root | `holoscan --project-root /path/to/module <command>` |

## 7. Troubleshooting

### The CLI rejects Python or WHEEL

The SDK 5 scaffold is C++ and DEB-only. This is intentional: do not add back the removed binding,
pytest, editable-import, or wheel infrastructure. Use a supported older template for an SDK 4
Module, or wait for the SDK and CLI to provide a supported SDK 5 Python template.

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
confirm that SDK 5.0 or later is installed and select it explicitly:

```bash
holoscan build my_module_pipeline --local --local-sdk-root /path/to/sdk --dryrun --verbose
```

After reviewing the preview, repeat it without `--dryrun`. You can also set
`HOLOSCAN_SDK_ROOT=/path/to/sdk` for the current shell.

### A command changes after a CLI update

Compare `holoscan version --json`, the installed command's `--help`, and the pins in
`requirements-cli.txt` and `pyproject.toml`. Keep the host, container, and development-group pins
synchronized when moving to a supported replacement.

### A dependency resolver warns about `ref`

Pin the dependency to a full 40-character commit SHA. Tags and branches can be convenient during
experimentation, but they are mutable and cannot reproduce a specific source tree reliably.

## 8. Next Steps

- Follow the companion [Use a Holoscan Module](../use-a-module/README.md) tutorial to consume the
  source or installed package from HoloHub and standalone applications.
- Review `modules/holoscan-gstreamer/` when an in-tree HoloHub descriptor is the better fit.
- [Holoscan CLI 5.x integration guide](../../../modules/cli-5x.md)
- Refer to the [Holoscan SDK User Guide](https://docs.nvidia.com/holoscan/sdk-user-guide/introduction/overview)
  for the broader SDK programming model.
- Read the [HoloHub contributing guide](../../../CONTRIBUTING.md) before proposing an in-tree
  descriptor or catalog change.

You now have the complete producer workflow: scaffold, implement, compile, test, package, inspect,
and prepare the Module for consumers. Happy Holocoding!
