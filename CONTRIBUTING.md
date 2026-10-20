# Contributing tutorials

Add new walkthroughs or improve existing ones in this repository. See the
[repository README](README.md) to choose and use a tutorial.

## What belongs here

A tutorial is a self-contained walkthrough that teaches a focused topic. Keep the
sample code and assets needed to follow it alongside the instructions. For work on
other parts of the ecosystem:

- Contribute complete applications to
  [Holoscan Applications](https://github.com/nvidia-holoscan/holoscan-applications)
  and performance benchmarks to
  [Holoscan Benchmarks](https://github.com/nvidia-holoscan/holoscan-benchmarks).
- Maintain reusable operators and Holoscan Modules in their own project repositories.
  Use the [HoloHub registry](https://github.com/nvidia-holoscan/holohub) to find them.
- Contribute CLI functionality and metadata schema changes to
  [Holoscan CLI](https://github.com/nvidia-holoscan/holoscan-cli).
- Report SDK bugs and request SDK features in
  [Holoscan SDK](https://github.com/nvidia-holoscan/holoscan-sdk/issues).

A tutorial may teach how to use any of these projects. Link to their canonical
repositories and document the version or commit used instead of duplicating their
implementation here.

## Anatomy of a tutorial

Each tutorial lives under `tutorials/` and owns its setup and execution instructions.
The repository has a small, optional CMake entry point for tutorials that support the
Holoscan CLI; it does not build every tutorial:

```text
tutorials/<topic>/
├── README.md       # Walkthrough
├── metadata.json   # Discovery, requirements, and tested SDK versions
└── ...             # Sources, notebooks, scripts, configuration, and visual assets
```

Related tutorials may share a parent directory and introductory README. Each individual
walkthrough still needs its own README and metadata.

For a CMake tutorial that supports `holoscan build`, `holoscan run`, or
`holoscan install`, add `add_holoscan_tutorial(<directory-name>)` to
`tutorials/CMakeLists.txt`. The directory must contain its own `CMakeLists.txt`
and have a CLI run configuration in metadata if it supports `holoscan run`.
The CLI selects it with `APP_<directory-name>=ON`. Tutorials without a
registered CMake application are skipped by the repository CMake entry point.

### `README.md`

The README is the tutorial's main walkthrough. Every tutorial README must include a
clear title and at least one splash image, diagram, or other visualization that helps
readers understand the topic or see what they will build.

The sections below are recommendations. Adapt their names, order, and content to suit
your tutorial and audience. You may combine or omit sections, or add others where useful.

| Suggested section | What to cover |
| --- | --- |
| Overview | A brief summary of the tutorial's purpose and what readers will learn |
| Description | Background and explanations that help readers follow the tutorial |
| Data | Any datasets the tutorial uses |
| Requirements | Supported platforms, hardware, SDK and CUDA versions, and other software dependencies |
| Tutorial Instructions | Setup, build, and run commands, with expected output or another way to verify success |
| Acknowledgements | Credits for collaborators and references to methods or resources used |

State the working directory for commands and whether they run on the host, in a container,
or on a target device. Document dependency versions, how to obtain any required data or
credentials, and where inputs and outputs belong. Finish with a result the reader can
check, such as an expected log message, generated artifact, or visualization.

Keep setup and runtime requirements in the tutorial, and ensure all relative links point
to existing files or directories. The Lychee pre-commit hook checks these local links.

### `metadata.json`

The `metadata.json` file is a machine-readable descriptor used to index and describe the
tutorial. It must follow the tutorial schema published by the Holoscan CLI:

- [`tutorial.schema.json`](https://github.com/nvidia-holoscan/holoscan-cli/blob/main/src/holoscan_cli/metadata/tutorial.schema.json)
- [`project.schema.json`](https://github.com/nvidia-holoscan/holoscan-cli/blob/main/src/holoscan_cli/metadata/project.schema.json)
  — the shared definitions that `tutorial.schema.json` references

Use these schemas as the reference for required fields, supported values, and metadata
guidance. Put tutorial fields under a single top-level `tutorial` object, alongside an
optional `$schema` string matching the selected schema's `$id`. Keep the descriptor's
requirements consistent with the README. Set `holoscan_sdk.minimum_required_version` to the
SDK minimum the tutorial needs and `holoscan_sdk.tested_versions` only to releases you
have actually tested.

The JSON pre-commit hook checks syntax, not schema compliance; review the descriptor
against the schemas as well.

## Adding a tutorial

1. Create a directory under `tutorials/` with a descriptive name, for example:

   ```bash
   mkdir -p tutorials/my-tutorial
   ```

2. In that directory, write `README.md` following the guidance above, and create
   `metadata.json` using the
   [Holoscan CLI tutorial schema](https://github.com/nvidia-holoscan/holoscan-cli/blob/main/src/holoscan_cli/metadata/tutorial.schema.json)
   as the reference. Set `version` to `0.1.0` with a matching `changelog` entry.
3. Add your sources alongside these files. Add an SPDX header to new source files, naming the year
   and your own organization, as existing files under `tutorials/` do:

   ```text
   SPDX-FileCopyrightText: Copyright (c) 2026, <COPYRIGHT HOLDER>. All rights reserved.
   SPDX-License-Identifier: Apache-2.0
   ```

4. Keep the tutorial self-contained. Everything needed to follow it should live in its
   directory or be fetched by its documented commands.
5. Follow the walkthrough from its documented starting environment, including setup and
   data acquisition. Confirm the expected result on a supported platform and record the
   commands and environment used. Run the validation below, then open a pull request.

When you update an existing tutorial, bump `version` and add a `changelog` entry for it.

## Document system changes and security-sensitive steps

Before a command that runs a privileged or `seccomp=unconfined` container, changes host
devices or kernel settings, or exposes network ports, explain why it is needed and what
it affects. Include cleanup or restoration instructions where applicable. For external
downloads, provide an integrity check when available and identify any unverified downloads
before the reader runs the step.

If the tutorial introduces a risk not already covered by the
[security threat model](SECURITY.md#threat-model), update it in the same pull request.
Report vulnerabilities through the private channels in [SECURITY.md](SECURITY.md).

## Validation

CI and local commits use the same [pre-commit](https://pre-commit.com/) checks.
Stage the new tutorial directory, for example with `git add tutorials/my-tutorial/`,
before using `--all-files`; that option otherwise misses new, untracked sources.

From the repository root, use Python 3.12 (the version used in CI), Git, Bash, and `curl`
to set up the CPU-only checks:

```bash
python3.12 -m venv .venv
.venv/bin/python -m pip install -r .github/requirements.txt
.venv/bin/pre-commit install
.venv/bin/pre-commit run --all-files --show-diff-on-failure
```

Pre-commit installs its pinned hook dependencies on the first run, which requires
network access to PyPI and to GitHub for the ShellCheck download. Later commits
automatically check staged files.
Whitespace and end-of-file hooks can fix files; review and stage those changes, then
retry the commit. Markdown's two-space hard line breaks are preserved.

The hooks check YAML and JSON syntax; merge conflict markers; private keys;
and files larger than 5 MiB. JSON checks exclude `.vscode/` files and
`devcontainer.json`, which permit comments and trailing commas. Keep large datasets
and generated assets outside Git and document how to obtain them in the tutorial.

Additional hooks validate sources and documentation:

- `check-python-compile` uses Python's built-in `py_compile` to check compilation
  without running the source. It may create ignored `__pycache__/` files.
- [ShellCheck](https://github.com/shellcheck-py/shellcheck-py) checks shell syntax and
  common mistakes, using each script's shebang to select its shell dialect.
- [nbcheck](https://github.com/furechan/nbcheck) validates notebook structure using
  `nbformat`, without executing cells or changing their outputs.
- [Lychee](https://github.com/lycheeverse/lychee) checks local Markdown links and image
  paths in the repository README and all Markdown files under `tutorials/`. Paths beginning
  with `/` resolve from the repository root.

Lychee runs on every commit, including deletions, so removing a linked file cannot
silently break an unchanged README. To run only this hook manually:

```bash
.venv/bin/pre-commit run lychee --all-files
```

Lychee runs offline; check external URLs and link anchors manually. `CONTRIBUTING.md`
is not among the hook's configured inputs, so check its links separately when editing it.
These hooks do not build SDK examples or execute notebooks, containers, or GPU workloads;
they do not replace the walkthrough test described above.

## Pull requests and review

Open a draft pull request if you want feedback while developing the tutorial. In the
description, summarize what it teaches or fixes and include:

- The platform and hardware tested, OS, SDK and CUDA versions, and container image or
  other environment details needed to reproduce the run.
- The validation and walkthrough commands you ran, their results, and any steps or
  platforms you could not test.

Merge requires passing validation, maintainer approval, and evidence that the walkthrough
works on a listed platform. If you cannot test the required hardware, describe what remains
unverified and ask a maintainer for help testing it.

## License and sign-off

Contributions are made under the Apache-2.0 [license](LICENSE).
Every commit must include a `Signed-off-by` trailer certifying the
[Developer Certificate of Origin](https://developercertificate.org/). Add it with:

```bash
git commit -s -m "Add <topic> tutorial"
```
