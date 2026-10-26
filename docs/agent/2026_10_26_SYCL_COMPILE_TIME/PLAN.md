# SYCL compile time: device passes only where a kernel is instantiated

**Status:** Open — written 2026-10-26; S1 to S5 are committed and built on 2026-10-26 (small 280 of 280, medium 614 of 615: `Medium_DataHandlesDeathTest.StateStopsOnAWindowPastTheEnd` failed once under four jobs and passed three runs alone), and section 6 waits for the owner.

## 1. Goal

When this work is done:

1. Inside this tree only the units that instantiate a kernel compile with `-fsycl` and the device
   targets. Every other unit compiles for the host with the toolchain's SYCL headers on its
   include path. Today every unit takes the device passes.
2. The SYCL policy of a target, which units take the device pass and what a binary links with,
   lives in one function file, `cmake/SushiRuntimeSyclTarget.cmake`. No directory-scope option
   and no CLI flag carries `-fsycl`.
3. The exported targets give a consumer what they gave before: the include roots, the library,
   the floating-point contract, `IntelSYCL::SYCL_CXX`, and `sushiruntime_gpu_offload` with the
   device targets, under the same names.
4. A public header includes SYCL only when its declarations use SYCL. Changes that keep the
   public API are made; changes that alter it are proposals in section 6.

Behaviour does not change; the owner's build runs the whole suite before and after.

## 2. What was measured, 2026-10-26

Read from the tree and from `build/compile_commands.json` of the last build (intel-llvm, CUDA
offload, distributed off). Nothing was compiled.

| Finding | Where | Cost |
| --- | --- | --- |
| All 475 units of the build compile with `-fsycl`; 429 also with `-fsycl-targets` (spir64 and an NVIDIA target), so each runs a host pass and two device passes | `compile_commands.json` | 182 header-check units, 45 module units, 186 test units and 16 example units pay device passes; SushiEngine measured a device pass at 50 to 72 percent of a kernel-free unit |
| The CLI passes `-DCMAKE_CXX_FLAGS=-fsycl` on Windows | `cli/sushiruntime/services/project.py` | Every unit of the tree and of SushiLog (46 units, no SYCL in them) compiles with `-fsycl` |
| `sushiruntime_sycl_configure` adds `-fsycl` with `add_compile_options` and `add_link_options` | `cmake/Sycl.cmake` | Every target of this directory and below; the toolchain file also decides compile policy |
| `sushiruntime_gpu_finalize` adds `-fsycl-targets` at directory scope as well as on `sushiruntime_gpu_offload` | `cmake/gpu/GpuBackend.cmake` | Two homes for one set of flags |
| Four functions call `add_sycl_to_target` each in their own way | `SushiRuntimeModule.cmake` (objects, header check), `SushiRuntimeTests.cmake`, `applications/examples/CMakeLists.txt` | No single place answers how a target compiles against SYCL |
| No library source instantiates a kernel | `modules/**/source/*.cpp` | The 86 module units need the SYCL headers only |
| Kernels are instantiated only through `submit_launch`, `submit_live_range`, `submit_late_window`, `device_sized_node` and `late_count_resolver_node`, or by a caller's own `parallel_for` and `single_task` | `api/detail/emit_*.hpp`, `late_window.hpp` | A guard in those five templates catches every API kernel at compile time |
| About 60 test units and 15 example units instantiate a kernel: they record a kernel through the API or call `parallel_for` or `single_task` | `tests/**`, `applications/examples/**` | These keep the device passes |
| 50 headers include `<sycl/sycl.hpp>`; 8 of them name nothing of SYCL | `buffer.hpp`, `state.hpp`, `runtime.hpp`, `step_driver.hpp` (public); `execution_engine.hpp`, `runtime_context.hpp`, `scheduler_execution_engine.hpp`, `sycl_compat.hpp` (internal) | Their includers parse SYCL for nothing unless another header needs it |
| Three public modules' headers need SYCL for their declarations: `api` (kernel shapes, `Extent`, `NdRange`, `sycl::queue& queue()`), `distributed` (`KernelRegistry::Kernel`), `machine` (`DeviceDescription::backend`, a `sycl::backend` member) | public headers | Proposals in section 6 |
| `sycl/sycl.hpp` compiled without `-fsycl` warns through `#pragma GCC warning`; on a system include path the warning is suppressed | the toolchain's `sycl/sycl.hpp` | Host units take the headers with `-isystem`, as SushiEngine and SushiAI do |

## 3. Design

- `cmake/Sycl.cmake` provisions the toolchain: it picks the compiler, finds the SYCL package
  and declares `IntelSYCL::SYCL_CXX`. On intel-llvm it puts the toolchain's include directory
  on that target, so every target that links it, in this tree and in a parent that adds it, finds
  `<sycl/sycl.hpp>` without `-fsycl`. It adds no compile or link option.
- `cmake/gpu/GpuBackend.cmake` selects the device targets and publishes them on
  `sushiruntime_gpu_offload` only.
- `cmake/SushiRuntimeSyclTarget.cmake` holds the policy: `sushiruntime_apply_sycl(<target>
  [SOURCES ...] [DEVICE_SOURCES ...])`. The `DEVICE_SOURCES` compile with `-fsycl` and the device
  targets; an executable or a shared library links with them. Where per-source compilation is
  not available (AdaptiveCpp, oneAPI, or no SYCL headers found) every unit takes the device pass,
  as today: slower, never wrong.
- `sushiruntime_add_module`, `sushiruntime_add_test_program`, `sushiruntime_add_module_tests` and
  `sushiruntime_add_example` take `DEVICE_SOURCES` and call the policy; the header check and the
  library call it with none.
- A unit that instantiates a kernel and is left off `DEVICE_SOURCES` must fail to build, not at
  run time. `api/detail/device_pass.hpp` gives the five kernel templates a `static_assert` that
  fires when a DPC++ unit compiled without `-fsycl` instantiates them. The checker
  `tools/layering/check_device_sources.py` covers the units that call `parallel_for` or
  `single_task` themselves, and refuses a listed unit that reaches no kernel.

## 4. Tasks

Hierarchy first: the guard that makes a wrong list fail, then the build functions with the lists,
then the CLI, then the checker, then headers.

| Id | Task | Waits on | Accepted when |
| --- | --- | --- | --- |
| S1 | `api/detail/device_pass.hpp` and its `static_assert` in the five kernel templates | nothing | a scratch unit that instantiates `submit_launch`, syntax-checked without `-fsycl`, fails with the guard's message, and each changed header syntax-checks alone |
| S2 | `cmake/SushiRuntimeSyclTarget.cmake`; `Sycl.cmake` and `GpuBackend.cmake` lose their directory-scope options; every target function calls the policy; the test and example build files list their device units | S1 | no file under `cmake/` adds `-fsycl` at directory scope, and the owner's regenerated `build.ninja` shows `-fsycl` on the listed units only |
| S3 | The CLI stops passing `-fsycl` in `CMAKE_CXX_FLAGS`, and the change reconfigures an existing tree | S2 | `cli/tests` pass |
| S4 | `tools/layering/check_device_sources.py` with its tests | S2 | the checker passes on the tree and its tests pass, one of them a fixture with a device unit left off the list |
| S5 | The 8 headers that name nothing of SYCL drop `<sycl/sycl.hpp>`; an includer that names SYCL includes it itself | nothing | each changed header syntax-checks alone without `-fsycl`, and no includer that names `sycl::` loses its last path to the header |

The owner's build follows S5: a clean build timed before and after, the count of units compiled
with `-fsycl`, the whole suite, and SushiBLAS, SushiAI and SushiEngine configured against the tree.

## 5. Not verified

- That DPC++ links host-only objects and device objects into one shared library and one
  executable when only the link line carries `-fsycl`. SushiEngine's build does it on this
  toolchain.
- The size of the gain. Section 2 gives the unit counts; the owner's build gives the times.
- The AdaptiveCpp and oneAPI paths, which keep whole-target device compilation; their configure
  is read, not run.

## 6. Proposals for the owner

| Proposal | Effect | Why it is not executed |
| --- | --- | --- |
| `machine::DeviceDescription::backend` becomes a SushiRuntime enum instead of `sycl::backend` | `machine/device_description.hpp`, a public module, stops including SYCL; its includers in `placement` and `wire` stop parsing it | Changes the layout and type of a public field |
| The package's `SushiRuntimeConfig.cmake` puts the consumer compiler's SYCL include directory on the `IntelSYCL::SYCL_CXX` it declares, as `Sycl.cmake` does in the build tree | An installed consumer can compile host units without `-fsycl` | Changes what the installed package gives a consumer |
| The `SYCL` keyword of `sushiruntime_add_module` splits into two: sources that include SYCL, and a header-only public module whose headers include it | One keyword, one meaning | Every module build file changes; it is a build interface of this repository |

## 7. Breaches and unneeded dependencies

Section 2 names each one found before the work; the orchestrator's report names what became of
each.
