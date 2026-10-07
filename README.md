# Holoscan Tutorials

Tutorials for the [NVIDIA Holoscan SDK](https://github.com/nvidia-holoscan/holoscan-sdk),

A tutorial here is a self-contained directory that teaches one topic: a `README.md`
walkthrough, a `metadata.json` descriptor, and whatever sources, notebooks, scripts, or
configuration the walkthrough refers to. This repository holds tutorial content only —
it is not the HoloHub CLI, and it has no repository-wide build.

Browse [`tutorials/`](tutorials/) to see what is available.

## Repository layout
```text
template/                      Starting files for a new tutorial
tutorials/<tutorial-name>/     One tutorial: README.md + metadata.json + sources
```

A tutorial directory may also group related sub-tutorials in subdirectories. In that case
the parent `README.md` introduces the group and each subdirectory carries its own
`metadata.json`.

## Using a tutorial

1. Read the tutorial's `README.md` and `metadata.json` first. Between them they state the
   supported SDK versions, platforms, hardware, dependencies, and data.
2. Install the SDK version the tutorial requires, using the
   [Holoscan SDK installation guide](https://docs.nvidia.com/holoscan/sdk-user-guide/setup/sdk-installation).
   Tutorials target different releases, and many need a GPU, containers, datasets, or
   additional hardware.
3. Follow the tutorial's own commands from its directory.

Some tutorials give instructions that run against a HoloHub checkout — anything invoking
`./holohub`, or referring to HoloHub applications, operators, modules, or benchmark data.
Those commands assume repository-root paths inside
[HoloHub](https://github.com/nvidia-holoscan/holohub) at the version the tutorial names,
so run them from a HoloHub checkout rather than from this repository.

## Anatomy of a tutorial

### `README.md`

The walkthrough. Start from [`template/README.md.template`](template/README.md.template),
which lays out the expected sections:

| Section | Contents |
| --- | --- |
| Title and splash image | The tutorial name, plus an image highlighting it |
| Overview | One sentence on the purpose and operations |
| Description | Detail and background for an average reader, with diagrams where they help |
| Data | Any datasets the tutorial uses |
| Requirements | Hardware and software a reader needs before building or running |
| Tutorial Instructions | The step-by-step walkthrough, including runnable commands |
| Acknowledgements | Methods and collaborators cited |

Sections that do not apply can be dropped, and topic-specific ones (troubleshooting,
references, performance results) can be added. Keep runtime requirements in the tutorial
itself rather than in this file, and make every relative link resolve — the validation
script checks them.

### `metadata.json`

The machine-readable descriptor, used to index and describe the tutorial. It follows the
`urn:holohub:tutorial:v1` schema published by the Holoscan CLI:

- [`tutorial.schema.json`](https://github.com/nvidia-holoscan/holoscan-cli/blob/main/src/holoscan_cli/metadata/tutorial.schema.json)
- [`project.schema.json`](https://github.com/nvidia-holoscan/holoscan-cli/blob/main/src/holoscan_cli/metadata/project.schema.json)
  — the shared definitions that `tutorial.schema.json` references

Every field lives under a single top-level `tutorial` object, alongside an optional
`$schema` string. Start from [`template/metadata.json.template`](template/metadata.json.template).

| Field | Required | Description |
| --- | --- | --- |
| `name` | yes | Human-readable tutorial name |
| `authors` | yes | Array of objects; each needs **both** `name` and `affiliation` |
| `version` | yes | Tutorial version, `MAJOR.MINOR[.PATCH]` with no leading zeros |
| `changelog` | yes | Object mapping each version to a one-line summary |
| `tags` | yes | Array of free-form strings used for discovery |
| `holoscan_sdk` | yes | Object with `minimum_required_version` and `tested_versions` (an array); both use the `version` format |
| `requirements` | yes | Object; may be `{}`. Defined keys are `data`, `gxf_extensions`, and `hardware` |
| `description` | no | One- or two-sentence summary |
| `platforms` | no | Array limited to `x86_64` and `aarch64` |
| `ranking` | no | Integer 0–5 self-assessment of maturity, lowest is most mature |
| `run` | no | Launch configuration: `command`, `workdir`, `docker_run_args`, `env` |

Notes on the fields that are easy to get wrong:

- **`requirements` entries.** `gxf_extensions` takes `{name, version}` objects. `hardware`
  takes `{name, description}` objects, optionally with `version` and a `required` boolean
  that defaults to `true`. `data` is a free-form array.
- **`ranking`.** The levels are defined in the
  [HoloHub contributing guide](https://github.com/nvidia-holoscan/holohub/blob/main/CONTRIBUTING.md):
  0 is production-ready and SDK-level, 3 is features under development, 4 is code of
  unknown quality, 5 is deprecated. Pick honestly; most tutorials sit at 1 or 2.
- **Extra keys.** The schema does not forbid properties it has not defined, so additional
  keys validate. `language` is the common one — it is defined in the shared project schema
  and most tutorials here set it (`Python`, `C++`, or an array of both).
- **`run.command`.** The Holoscan CLI expands placeholders and parses this into an argument
  vector without an implicit shell. Shell syntax such as pipes or `&&` will not work.

## Adding a tutorial

1. Create `tutorials/<tutorial-name>/` and copy in the two template files, dropping the
   `.template` suffix:

   ```bash
   mkdir -p tutorials/<tutorial-name>
   cp template/README.md.template tutorials/<tutorial-name>/README.md
   cp template/metadata.json.template tutorials/<tutorial-name>/metadata.json
   ```

2. Write the walkthrough and fill in the metadata. Set `version` to `0.1.0` with a matching
   `changelog` entry, and set `holoscan_sdk.tested_versions` to the versions you actually
   ran against.
3. Add your sources alongside them. Add an SPDX header to new source files, naming the year
   and your own organization, as existing files under `tutorials/` do:

   ```text
   SPDX-FileCopyrightText: Copyright (c) 2026, <COPYRIGHT HOLDER>. All rights reserved.
   SPDX-License-Identifier: Apache-2.0
   ```

4. Keep the tutorial self-contained. Everything needed to follow it should live in its
   directory or be fetched by its documented commands.
5. Run the validation below, then open a pull request.

When you update an existing tutorial, bump `version` and add a `changelog` entry for it.

## Validation

Run the CPU-only validation checks from the repository root:

```bash
python3 -m venv .venv
.venv/bin/python -m pip install -r .github/requirements.txt
.venv/bin/python .github/validate.py
```

The script covers this file and everything under `tutorials/`, checking that:

- Python files compile and shell scripts pass `bash -n`
- JSON files are strict JSON — no comments, no trailing commas. `.vscode/` files and
  `devcontainer.json` are excluded, since those formats permit both.
- Notebooks pass `nbformat` validation
- Relative Markdown links and image paths point at files that exist

External URLs, link anchors, and the contents of `template/` are not checked. The script
does not build SDK examples or execute notebooks, containers, or GPU workloads — validate
runtime behavior by following the tutorial yourself.

## License

Contributions are made under the Apache-2.0 [license](LICENSE).
