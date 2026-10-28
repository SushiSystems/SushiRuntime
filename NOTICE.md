# Notices

SushiRuntime is Copyright (c) 2026-present Mustafa Garip & Sushi Systems and licensed under the PolyForm Noncommercial
License 1.0.0; see `LICENSE`. The material below keeps its own licence.

No third-party source is vendored in this repository and no file is ported from third-party
code. Every component below is fetched or installed at build time.

## Third-party components

| Component | Where | Source | Version | Licence |
| --- | --- | --- | --- | --- |
| hwloc | Linked by the runtime; `cmake/Dependencies.cmake`, `[hwloc]` in `cli/sushistack.deps.toml` | https://github.com/open-mpi/hwloc | Not pinned here; vcpkg port 2.11.2 was read | BSD-3-Clause; read from the vcpkg port's `copyright` file |
| GoogleTest | Tests only, not installed; `find_package(GTest)` in `CMakeLists.txt` | https://github.com/google/googletest | Not pinned here; vcpkg port 1.17.0 was read | BSD-3-Clause, Copyright 2008 Google Inc.; read from the vcpkg port's `copyright` file |
| intel/llvm SYCL toolchain | Compiler and SYCL runtime; `[intel-llvm]` in `cli/sushistack.deps.toml`, `Dockerfile` | https://github.com/intel/llvm | Nightly bundle; `Dockerfile` pins `nightly-2026-06-12` | Apache-2.0 WITH LLVM-exception; read from `sycl/LICENSE.TXT` on the upstream `sycl` branch |
| Unified Runtime | Loaded by the SYCL runtime; ships inside the intel/llvm bundle | https://github.com/intel/llvm, `unified-runtime/` | That of the bundle | Apache-2.0 WITH LLVM-exception; read from `unified-runtime/LICENSE.TXT` at commit `d5f649b7` |
| AdaptiveCpp | Secondary SYCL toolchain; `[adaptivecpp]` in `cli/sushistack.deps.toml`, `Dockerfile` | https://github.com/AdaptiveCpp/AdaptiveCpp | v24.10.0 | BSD-2-Clause, Copyright The AdaptiveCpp Contributors; read from upstream `LICENSE` at the tag |
| Intel OpenCL CPU runtime (`intel-oneapi-runtime-opencl`) | Installed in every Docker image; `Dockerfile` | https://apt.repos.intel.com/oneapi | Not pinned | Intel's end user licence; unverified |
| Intel oneAPI DPC++ compiler (`intel-oneapi-compiler-dpcpp-cpp`) | One of three SYCL toolchains, off by default in the image (`WITH_ONEAPI`); `[oneapi]` in `cli/sushistack.deps.toml`, `Dockerfile` | https://apt.repos.intel.com/oneapi | Not pinned | Intel's end user licence; unverified |
| NVIDIA CUDA toolkit | Optional GPU backend; `[cuda]` in `cli/sushistack.deps.toml`, base image in `Dockerfile` | https://developer.nvidia.com/cuda-toolkit | Image `nvidia/cuda:12.4.1-devel-ubuntu22.04` | NVIDIA's end user licence; unverified |
| typer | `sr` command line; `cli/pyproject.toml` | https://pypi.org/project/typer/ | `>=0.12`; 0.26.7 was read | MIT; read from the installed package metadata |
| rich | `sr` command line; `cli/pyproject.toml` | https://pypi.org/project/rich/ | `>=13.0`; 15.0.0 was read | MIT; read from the installed package metadata |
| tomli | `sr` command line on Python below 3.11; `cli/pyproject.toml` | https://github.com/hukkin/tomli | `>=2.0` | MIT, Copyright (c) 2021 Taneli Hukkinen; read from upstream `LICENSE` on `master` |
| sushicore | `sr` command line; `cli/pyproject.toml` | https://github.com/SushiSystems/SushiCore | `==0.9.7` | A Sushi Systems package under its own licence |

The Python packages those four pull in were not read.

## Redistributed binaries

`cmake/SushiRuntimeInstall.cmake` stages these Windows libraries into the install tree under the
component `sushiruntime_redist`. The default install does not carry them.

| Binary | Component | Comes from | Licence |
| --- | --- | --- | --- |
| `hwloc-15.dll` | hwloc | vcpkg `hwloc` | BSD-3-Clause |
| `sycl.dll`, `sycl<N>.dll` | intel/llvm SYCL runtime | The intel/llvm bundle, beside the compiler | Apache-2.0 WITH LLVM-exception |
| `ur_loader.dll`, `ur_adapter_opencl.dll`, `ur_adapter_level_zero.dll`, `ur_adapter_level_zero_v2.dll`, `ur_win_proxy_loader.dll` | Unified Runtime | The intel/llvm bundle, beside the compiler | Apache-2.0 WITH LLVM-exception |

Whether these binaries may be redistributed is still open. The intel/llvm bundle ships no
licence or redistribution file, the licences above were read from the upstream source and not
from the bundle, and what the bundle's binaries link in was not examined. No archive that holds
the `sushiruntime_redist` component leaves the machine until that is settled, and such an
archive must carry the licence texts of the three components.

## Earlier versions

The repository is private. The tags `v0.2.0` (`8f190c1`) and `v0.3.0` (`3218aa8`) carry the
Apache License 2.0 as `docs/LICENSE`; `v0.3.0` is also a release on the private GitHub
repository, with no assets. The commits from `0597255` (2026-09-19) up to the licence change
carried the Apache License 2.0 with LLVM exceptions and were never tagged. Anyone who received
one of those states holds it under the licence it carried. The tag `v0.4.0` carries the PolyForm Noncommercial License 1.0.0 as `LICENSE`.
