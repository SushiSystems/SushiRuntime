# Global handle: report

What each task of [the plan](PLAN.md) did, with its evidence.

## T03: the integrated-GPU probe

Run on 2026-11-15 on the first machine (Ryzen 7600X with its integrated GPU, RTX 3080 Ti),
Windows 10 IoT LTSC 21H2.

| Step | Result | Evidence |
| --- | --- | --- |
| HIP lists gfx1036 | pass | `hipInfo`: `AMD Radeon(TM) Graphics`, `gcnArchName: gfx1036`, 6.17 GB |
| `ur_adapter_hip.dll` builds | pass | built from the Unified Runtime source the CUDA adapter uses, MSVC and Ninja, Release, `-DUR_BUILD_ADAPTER_HIP=ON -DUR_HIP_PLATFORM=AMD` |
| A kernel compiles for `gfx1036` | pass | `hipcc --offload-arch=gfx1036`, and the SYCL compiler with `amdgcn-amd-amdhsa` beside `nvptx64-nvidia-cuda` in one binary |
| `sycl-ls` lists the device | pass | `[hip:gpu][hip:0] AMD HIP BACKEND, AMD Radeon(TM) Graphics gfx1036` beside `[cuda:gpu][cuda:0] ... RTX 3080 Ti` |
| The kernel's bytes equal the CPU's | pass | one SYCL binary, 2^20 floats, `y = x * 1.5 + 0.25` with `-ffp-contract=off`: `bytes_equal=1` on the integrated GPU and on the 3080 Ti |

The route differs from the one the specification names, and the differences are part of the result.

- **No HIP SDK installer.** AMD's TheRock Windows package `therock-dist-windows-gfx103X-all-10.1.0`
  is unpacked under `D:\Projects\.sushisystems\rocm\10.1.0`. It needs no administrator and leaves
  the Adrenalin driver in place, whose `amdhip64.dll` the runtime uses.
- **Device libraries from ROCm 7.0.** ROCm 10.1's device bitcode is written by LLVM 24, which the
  SYCL compiler's LLVM 21 cannot read (`Unknown attribute kind (106)`). ROCm 7.0's `rocm-device-libs`
  (LLVM 20, operating-system independent) are read; they sit under
  `D:\Projects\.sushisystems\rocm\devlibs-7.0`.
- **`wchar_t` on the device.** Those libraries carry a 4-byte `wchar_t` flag and Windows device code a
  2-byte one, so the AMD device frontend takes `-fno-short-wchar`. No kernel uses `wchar_t`.
- **The adapter is loaded by name.** The loader does not look for `ur_adapter_hip.dll` on Windows;
  `UR_ADAPTERS_FORCE_LOAD` lists it with the CUDA and OpenCL adapters.
- **A fault at exit.** A process that used the HIP adapter ends with `0xC0000409` after its work;
  its results are correct and a CUDA-only process exits cleanly. KI-33 tracks it.

The AdaptiveCpp route was not needed. T38 puts this route into `sr build`.
