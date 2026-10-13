# Security Policy: Holoscan Tutorials

## Reporting a Vulnerability

If you discover a potential security vulnerability, **do not open a public issue,
pull request, or discussion**.

- **Web (preferred):** Report it through the
  [NVIDIA Vulnerability Disclosure Program](https://www.nvidia.com/en-us/security/).
- **Email:** Send details to [psirt@nvidia.com](mailto:psirt@nvidia.com). For
  encrypted communication, use the
  [NVIDIA public PGP key](https://www.nvidia.com/en-us/security/pgp-key).
- **GitHub:** Use this repository's **Security** tab to submit a private report
  on `nvidia-holoscan/holoscan-tutorials`.

Include the project name and affected version or branch, the tutorial directory,
vulnerability type, reproduction steps, proof-of-concept code when available, and
an impact assessment. Detailed reports help NVIDIA evaluate and address issues
faster.

NVIDIA's Product Security Incident Response Team (PSIRT) will acknowledge the
report, validate and assess the issue, develop and test fixes, and publish a
security bulletin when appropriate.

## Supported Versions

Security fixes are applied to the current `main` branch. This repository is a
collection of independent tutorials, not a single versioned product. Each
tutorial's `metadata.json` records the Holoscan SDK versions it was tested
against. Several walkthroughs also name a container tag, such as Holoscan
`v4.6.0` in the VS Code dev container and CloudXR Runtime `5.0.1`.

## Security Architecture and Context

Holoscan Tutorials is documentation and sample code for the NVIDIA Holoscan SDK.
Each directory under `tutorials/` is a self-contained walkthrough: a `README.md`,
a `metadata.json` descriptor, and the sources, notebooks, scripts, or container
files needed to follow that topic. There is no repository-wide service, package,
or installer. `.github/validate.py` only syntax-checks sources and local
Markdown links.

This project operates at the tutorial and sample-application level. Its primary
security responsibility is to keep the commands and samples a reader runs inside
the host, devices, and data directories that reader chose. It does not
authenticate users, listen as a product service, or ship a packaged runtime.

The principal security boundaries are:

- Host-tuning walkthroughs in `tutorials/high_performance_networking/` and
  `tutorials/windows_vm/` document root commands that change NIC firmware
  settings, kernel modules, hugepages, CPU isolation, and VFIO GPU binding, and
  they start Holoscan networking containers with Docker `--privileged`.
- `tutorials/cuda_mps/start_mps_daemon.sh` starts `nvidia-cuda-mps-control` and
  points `CUDA_MPS_PIPE_DIRECTORY` and `CUDA_MPS_LOG_DIRECTORY` at `/tmp`.
- Debugging samples disable container restrictions.
  `tutorials/debugging/cli_debugging/debug_gdb.sh` and `debug_legacy.sh` pass
  `--security-opt seccomp=unconfined`. The VS Code dev container in
  `tutorials/debugging/holoscan_container_vscode/.devcontainer/devcontainer.json`
  uses `--net=host`, `--cap-add=SYS_PTRACE`, and `seccomp=unconfined`, bind-mounts
  the X11 socket and the host `XDG_RUNTIME_DIR`, and enables
  Docker-outside-of-Docker. Its `Dockerfile` grants the `holoscan` user
  passwordless sudo.
- `tutorials/cloudxr_runtime_for_xr_applications/README.md` opens host firewall
  ports and publishes CloudXR Runtime TCP `48010` and UDP `47998`–`48000`,
  `48005`, `48008`, and `48012`. `tutorials/local-llama/chatbot.py` sends chat
  text to `http://127.0.0.1:8081` with an empty `openai.api_key` and calls
  `demo.launch()`.
- `tutorials/creating-multi-node-applications/` splits Holoscan applications
  across fragments and tells the reader to pass `--driver`, `--worker`, and
  `--address <node IP>:<port>`. `scenario2/endoscopy_distributed_app.py` can
  take an AJA or Yuan capture source with RDMA and records tensors under the
  `/tmp` directory named in `endoscopy_distributed_app.yaml`.
- `tutorials/dicom_to_usd_with_monai_and_holoscan/tutorial.py` loads a DICOM
  study from the MONAI Deploy input path and writes a mesh and USD stage to the
  output path. Its `Dockerfile` takes `ARG BASE_IMAGE`, installs unpinned
  `requirements.txt` with pip, and downloads a sample bundle with `gdown` and
  an aarch64 OpenUSD archive with `wget`, without checksums.
- `tutorials/gpu_direct_storage_on_holoscan/holoscan_gds.py` reads and writes
  GPUDirect Storage files through `kvikio.CuFile`. The default path is
  `/mnt/nvme/data/test-file`; `config.yaml`, when present, is loaded with
  `yaml.safe_load`.
- `tutorials/holohub_operators_external_applications/holohubCLI_wrapper` installs
  `holoscan-cli` by expanding `HOLOSCAN_CLI_INSTALL_ARGS` unquoted into
  `pip install`, defaulting to a pre-release from `https://pypi.nvidia.com`.
- `tutorials/pretrained_foundational_models/self_supervised_training/` loads
  checkpoints with `torch.load` and builds from
  `nvcr.io/nvidian/pytorch:22.02-py3`. `extract_frames.py` reads every `.mp4`
  name under a caller-supplied video directory.
- `tutorials/holoscan_response_time_analysis/artifact/` builds and launches
  local experiment binaries with `os.system` and creates result files with mode
  `0o666`.
- `tutorials/holoscan-playground-on-aws/README.md` documents an EC2 instance
  with a public IP and SSH key, then runs a Holoscan container with `--net host`,
  `--ipc=host`, `--cap-add=CAP_SYS_PTRACE`, and `xhost +local:docker`.
- GitHub Actions (`.github/workflows/ci.yml`) checks out the repo with
  `persist-credentials: false` and `contents: read`, installs the pinned
  `.github/requirements.txt`, and runs `validate.py`. That job does not build
  images or execute tutorials.

### Threat Model

1. **Privileged host and device reconfiguration:** The high-performance
   networking tutorial runs Holoscan containers as root with `--privileged`,
   loads `nvidia_peermem`, changes ConnectX link type with `mlxconfig`, and
   writes hugepage and CPU-isolation settings. The Windows VM tutorial binds a
   GPU to `vfio-pci` through sysfs `driver_override` and starts it under QEMU.
   A reader who follows those steps gives the container or the VM control of
   host devices and scheduling, beyond the tutorial process.
2. **Debugging containers weaken isolation:** `debug_gdb.sh` and the VS Code
   `devcontainer.json` start containers with `seccomp=unconfined` and, for the
   dev container, `SYS_PTRACE` and `--net=host`. The dev container Dockerfile
   adds passwordless sudo, and the dev container bind-mounts the host X11
   socket and runtime directory and can talk to the host Docker socket. A
   process inside that container can trace other processes and reach host
   services and the display.
3. **Network exposure from streaming, chat, and distributed fragments:** The
   CloudXR tutorial allows and publishes the CloudXR Runtime ports on the host.
   `chatbot.py` posts the Gradio chat history to a local OpenAI-compatible
   server that is configured with an empty API key, and `demo.launch()` serves
   the UI. Multi-node samples connect fragments with `--address` and carry
   endoscopy video, including AJA RDMA capture, between those addresses. None
   of these samples authenticate the peer or wrap the traffic in TLS.
4. **Unverified downloads become part of the tutorial environment:** The DICOM
   Dockerfile fetches a Google Drive bundle and an Omniverse OpenUSD archive
   without checksums and installs unpinned Python requirements. The
   self-supervised Dockerfile starts from an internal PyTorch image and
   installs unpinned packages. `holohubCLI_wrapper` word-splits
   `HOLOSCAN_CLI_INSTALL_ARGS` into `pip install`. The local-llama walkthrough
   downloads a GGUF weight file. A substituted URL, index, or environment value
   is executed or loaded as part of the environment the reader then runs.
5. **Caller-selected medical, video, and GPUDirect files:** `tutorial.py`
   reads whatever DICOM folder the MONAI Deploy input path names.
   `holoscan_gds.py` reads `--out_path` or `config.yaml` and writes
   `/mnt/nvme/data/test-file-complete` through GPUDirect Storage.
   `downstream_task_tool_segmentation.py` unpickles checkpoints with
   `torch.load`. `extract_frames.py` opens every `.mp4` under `--datadir`.
   The endoscopy YAML records tensors to `/tmp`. Those paths are trusted local
   inputs; a substituted study, checkpoint, or device file is processed with
   the reader's GPU and filesystem access.
6. **CUDA MPS control socket under `/tmp`:** `start_mps_daemon.sh` launches
   `nvidia-cuda-mps-control -d` with its pipe and log directories in `/tmp`.
   On a multi-user host, another local user who can reach that pipe can submit
   work to the MPS server running as the reader who started the daemon.

### Critical Security Assumptions

- The reader controls the host, container runtime, Docker flags, firewall, and
  cloud security group. This repository does not restrict `--privileged`,
  `seccomp=unconfined`, device nodes, VFIO binding, or GPU access.
- The Holoscan SDK, CUDA, GPUDirect Storage, DOCA, the NGC base images, and
  CloudXR Runtime behave as trusted platform software and enforce their own
  device and memory contracts.
- Tutorial paths, YAML files, checkpoints, videos, DICOM folders, environment
  variables, and CLI arguments are operator configuration, not untrusted
  network input.
- Distributed fragment traffic uses the address the reader passes to
  `--address`. These samples do not configure authentication or TLS for that
  link.
- `chatbot.py` is used against a loopback Llama server. The sample does not
  check that `openai.api_base` stays on `127.0.0.1` or that Gradio remains
  unbound from other interfaces.
- The CUDA MPS daemon and the host permissions on `/tmp/nvidia-mps` limit who
  can connect to the control pipe.
- Secrets such as an EC2 SSH key or an NGC API key are supplied by the reader
  and are not committed to this repository. CI does not persist checkout
  credentials.
- Third-party Python, apt, and container packages are installed by the reader
  while following a tutorial. They are not vendored in this source tree.
- `validate.py` compiles and syntax-checks tutorial files. It does not execute
  them, build containers, or contact external URLs.

## Dependency and Distribution Security

This repository redistributes NVIDIA Apache-2.0 tutorial source, scripts, and
figures. It does not vendor third-party source trees. Tutorial workflows
install third-party packages at build or run time, including unpinned entries
in `tutorials/local-llama/requirements.txt` and
`tutorials/dicom_to_usd_with_monai_and_holoscan/requirements.txt`. New or
upgraded third-party dependencies must have their license and redistribution
terms reviewed before they are pinned, installed into a published image, or
shipped. Components distributed with a Holoscan product must follow the
applicable NVIDIA Open Source Review Board process. There is no `NOTICE` file
in this repository.
