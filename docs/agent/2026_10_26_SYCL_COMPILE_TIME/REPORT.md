# SYCL compile time: device passes only where a kernel is instantiated: report

**Status:** Open — written 2026-10-26 by the SushiAI orchestrator from the worker's reply; S1 to S5 are committed and syntax-checked, section 6 of the plan waits for the owner.

## Dispatches

dispatch: SushiRuntime SYCL compile plan and S1 to S5 — opus medium, 175 tool calls, passed, rung 1

## 1. What was done

| Commit | Change |
| --- | --- |
| `f5d3c22a` | The plan |
| `e6c541f9` | `api/detail/device_pass.hpp`: `Detail::require_device_pass<K>()` refuses at compile time a kernel recorded in a unit compiled without `-fsycl`; `submit_launch`, `submit_live_range`, `submit_late_window`, `device_sized_node` and `late_count_resolver_node` call it |
| `3e4e89e9` | `cmake/SushiRuntimeSyclTarget.cmake` holds the whole policy. `Sycl.cmake` and `GpuBackend.cmake` set no directory-scope `-fsycl`; `IntelSYCL::SYCL_CXX` carries the toolchain's include directory; the module, header-check, library, test and example functions take `DEVICE_SOURCES`, 49 test units and 16 example units |
| `c0efd348` | The CLI stops passing `-fsycl` in `CMAKE_CXX_FLAGS` on Windows, and the flags join the configure stamp |
| `375be31c` | `tools/layering/check_device_sources.py` and its six tests |
| `8e3c8c12`, `3326870a`, `b5e26b9c` | Eight headers drop `<sycl/sycl.hpp>`, which they did not use |
| `1a749fc9` | The plan records the tasks |

Expected on intel-llvm: 65 units compile with `-fsycl`, where all 475 of the last build did, 429
of them with two device targets. The library itself has no kernel unit.

## 2. Verification

`clang++ -fsyntax-only` with the flags of `compile_commands.json`, one unit at a time. A unit that
calls `submit_launch` without `-fsycl` exits 1 with the guard's message, and 0 with it. The guarded
headers, `device_pass.hpp`, the eight cleaned headers, the ten that lost their path to SYCL and
`step_machinery.cpp` compile for the host. `check_all` reports only the two `check_test_mirror`
findings that existed before; the CLI's pytest passes 155 tests.

The first SushiAI build against this tree, the same night, showed the guard working: it refused a
SushiAI unit that included SushiBLAS's umbrella, whose inline `counting_sort` launches kernels.

## 3. Proposals for the owner

1. `DeviceDescription::backend` becomes a SushiRuntime enum instead of `sycl::backend`; a public
   field changes.
2. The installed `SushiRuntimeConfig.cmake` puts the consumer's SYCL include directory on its
   `IntelSYCL::SYCL_CXX` stub.
3. The two meanings of the `SYCL` keyword of `sushiruntime_add_module` split.

## 4. SOLID breaches and unneeded dependencies

Fixed: the directory-scope `-fsycl` of `Sycl.cmake`; the flags set both at directory scope and
on `sushiruntime_gpu_offload`; four homes of `add_sycl_to_target`; `-fsycl` in the CLI's
`CMAKE_CXX_FLAGS`, which sent SushiLog's 46 units through the device passes; eight unused SYCL
includes. Kept: `device_description.hpp` (proposal 1); `AdvancedControls::queue()` returning
`sycl::queue&`, the escape hatch; the double meaning of `SYCL` (proposal 3); the library's
`-fsycl` link, which links libsycl. Seen and not changed: the Windows oneAPI step that adds
`-fsycl` to `CMAKE_CXX_FLAGS`; the inline SYCL body of `device_probe.hpp`; a stale copy of the CLI
under `cli/build/lib`; `tests/applications/examples` listing its sources through variables.

## 5. What the owner's build must confirm

1. `build.ninja` shows `-fsycl` on the 65 listed units only, and no guard fires.
2. Host units compile under `-Werror` with SYCL on the system include path.
3. The library, tests and examples link, and the whole suite passes, Large tests included.
4. The old `build/` reconfigures and its `CMAKE_CXX_FLAGS` ends up empty.
5. The sanitizer build and its tests pass.
6. The clean build time before and after.
7. SushiBLAS, SushiAI and SushiEngine configure and build against this tree.
8. The AdaptiveCpp and oneAPI configures fall back to whole-target device compilation.
