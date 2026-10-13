# Contributing to Holoscan Tutorials

Thanks for helping other developers learn the
[NVIDIA Holoscan SDK](https://github.com/nvidia-holoscan/holoscan-sdk)! To contribute,
fork this repository, add or update a directory under `tutorials/`, validate it, and open a
pull request with signed-off commits:

```bash
git commit -s -m "Add my tutorial"
```

The [README](README.md) describes what a tutorial contains. Follow its
[Anatomy of a tutorial](README.md#anatomy-of-a-tutorial),
[Adding a tutorial](README.md#adding-a-tutorial), and [Validation](README.md#validation)
sections. This guide covers the rest of the process.

## What belongs here

This repository holds tutorials: self-contained walkthroughs that teach one topic. Other
contributions have a different home:

- Applications, operators, benchmarks, and Holoscan Modules go to
  [HoloHub](https://github.com/nvidia-holoscan/holohub). A tutorial may walk through
  them, but their code lives in HoloHub.
- Holoscan CLI changes and the tutorial metadata schema go to
  [Holoscan CLI](https://github.com/nvidia-holoscan/holoscan-cli).
- SDK bugs and feature requests go to the
  [Holoscan SDK](https://github.com/nvidia-holoscan/holoscan-sdk/issues).

## Review and acceptance

Open a draft pull request early if you want feedback while you work. When the tutorial is
ready, mark the pull request ready for review. It is accepted once:

- the validation workflow passes,
- a Holoscan maintainer approves it, and
- a maintainer has followed the tutorial on a listed platform, or the pull request
  describes how you tested it.

## Security-sensitive steps

Some tutorials ask readers to reconfigure their system. If yours runs privileged or
`seccomp=unconfined` containers, changes host devices or kernel settings, opens network
ports, or downloads files without checksums, say so in the tutorial before the step. Then
add the risk to the threat model in [SECURITY.md](SECURITY.md).

## Signing your contribution

Every commit must carry a `Signed-off-by` trailer, added by `git commit -s`. Signing off
certifies the [Developer Certificate of Origin](https://developercertificate.org/): you
wrote the contribution, or otherwise have the right to submit it under the
[Apache-2.0 license](LICENSE). Pull requests with unsigned commits are not accepted.

To report a security vulnerability, follow [SECURITY.md](SECURITY.md) instead of opening a
public issue.
