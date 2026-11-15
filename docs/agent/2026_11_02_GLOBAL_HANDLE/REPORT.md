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

## Dispatches

```
dispatch: T38 — sonnet medium, 62 turns, passed, rung 1
dispatch: T01 — opus low, 37 turns, passed, rung 1
dispatch: T05 — opus low, rung 1, passed
dispatch: T02 — sonnet medium, rung 1, passed
dispatch: T04 — sonnet low, rung 1, passed
dispatch: T12 — opus low, rung 1, passed
dispatch: T06 — opus low, 270 turns, rung 1, passed
dispatch: T08 — sonnet low, 18 turns, rung 1, passed
dispatch: T09 — haiku high, rung 1, failed (stopped on an interface ruling)
dispatch: T23 — opus low, rung 1, passed
dispatch: T31 — haiku high, 124 turns, rung 1, passed (SushiEngine e3348900f)
dispatch: T10 — sonnet medium, 146 turns, rung 1, passed
dispatch: T13 — opus low, 145 turns, rung 1, passed
dispatch: T17 — sonnet medium, rung 1, passed
dispatch: T24 — sonnet medium, rung 1, passed
dispatch: T28 — sonnet medium, 108 turns, rung 1, passed
dispatch: T18 — sonnet medium, rung 1, passed
dispatch: T15 — sonnet medium, 48 turns, rung 1, passed
dispatch: T16 — haiku high, 96 turns, rung 1, passed
dispatch: T14 — sonnet medium, 102 turns, rung 1, passed
dispatch: T20 — sonnet medium, 74 turns, rung 1, passed (written; measured in the closing run)
dispatch: T09 — opus low, 235 turns, rung 2, passed
dispatch: T07 — sonnet low, 46 turns, rung 1, passed
dispatch: T41 — sonnet medium, rung 1, passed
dispatch: T29 — opus low, rung 1, passed (skipped loading four style skills; checkers clean)
dispatch: T39 — opus low, 122 turns, rung 1, passed
dispatch: T21 — opus low, rung 1, passed
dispatch: T37 — opus low, 137 turns, rung 1, passed
dispatch: T40 — sonnet medium, 33 turns, rung 1, passed
dispatch: T42 — sonnet low, rung 1, passed
dispatch: T11 — haiku medium, rung 1, passed (skipped loading the style skills; checkers clean)
dispatch: T45 — opus low, 91 turns, rung 1, passed
dispatch: T19 — opus low, 71 turns, rung 1, passed
dispatch: T25 — opus low, rung 1, passed (skipped loading the style skills; checkers clean)
dispatch: T44 — opus low, 229 turns, rung 1, passed (one ruling refined by the orchestrator)
dispatch: T22 — haiku high, 81 turns, rung 1, passed
dispatch: T26 — sonnet medium, rung 1, passed
dispatch: T43 — opus low, 132 turns, rung 1, passed (skipped loading the style skills; checkers clean)
```

## T05: the residency directory

- Retired slots do not pass through epoch-based reclamation. Chunks are never freed, so no memory
  waits on readers, and a reader that races a retire sees the raised generation on its recheck.
  Deferring reuse to the epoch scan would make the next handle depend on reclamation timing and
  break "same program, same handles". Decision taken by the orchestrator under the owner's
  delegation of 2026-11-15.
- `ResidencyDirectory::entry_address` is public only for the chunk-stability test. It exposes a
  read-only internal address; a test-only seam would remove it.

## T06: the handle replaces the address

- `RuntimeContext::handles()` hands out the concrete `ResidencyDirectory`, because the API layer
  mints and describes; the engine, the scheduler and the dispatcher take `const HandleResolver&`.
- `BorrowedIndex` takes one mutex on borrow and release. Neither sits on the dispatch path.
- A borrowed range that partly overlaps a live borrowed range is refused.
- Syntax-checked: every changed module source and most tests. Not checked one by one: some
  profiling, dynamic-graph and recorder tests, `test_replay`, the benchmarks and most examples.
  The closing build covers them, since a `BufferRegion` no longer converts from a pointer.

## T09: the ruling on USMAllocator

`USMRegistry` is the pointer table inside `USMAllocator`, and about twenty files ask it for a
pointer's device, kind, owner and base. The orchestrator ruled, under the owner's delegation:
`USMAllocator` allocates and frees and keeps no table; the directory entry records the instance
and its kind (host, shared, device); every pointer query becomes a query on the handle; raw
memory a caller brings is reached through `BorrowedIndex` only. An address index for owned
memory would be the registry under a new name, against "address-to-handle serves borrowed
memory only". T09 runs again one rung up, after T13, because it touches `runtime_context` too.

## T31: SushiEngine's runtime adapter

Committed in SushiEngine as `e3348900f` on `feature/global_handle`, in the worktree
`D:/Projects/.worktrees/sushiengine-global_handle`. The adapter maps each buffer's `ResourceID`
to its `GlobalHandle` through a table one runtime owns. Memory the runtime did not allocate fails
its verify; the adapter has no borrowed path, because a whole raw interval has no extent to
borrow. SushiEngine pins a SushiRuntime package older than the handle, so it builds only once the
closing build moves its pins. SushiAI's build runs in the closing build.

## T10: shared buffers by handle

- `SharedBuffers` keeps each shared buffer's version and size by handle, because the directory
  holds no versions. `wire_id_of(GlobalHandle)` derives the wire id until T28 folds it away.
- `Cluster::share(void*, bytes)` is gone; raw memory enters as a borrowed handle.
- The release path still starts from the address observer in `platform/memory`; T09 replaces it
  with a retire notification, and the coordinator then unshares from that.

## T13: the coherence planner

The planner, the copy plan, the planned nodes and the composer hook are in `engine/coherence`, and
the span refusal is gone. The acceptance holds on fakes. On a real runtime three pieces are
missing, each now a task: kernels still read the home address captured at `add()` (T39),
`placement_of` refuses a node across SYCL contexts (T40), and no transfer path is registered
(T17). The planner takes the concrete directory (T41), every device gets a pool of its own (T42),
and planned buffers are allocated and freed every step and copied back home at graph end, which
T14 and T21 remove.

## T18: the host-staged path

The staging ring's pinned memory is allocated in the first device's SYCL context. On a device of
another vendor the same memory is pageable host memory to that context, so a copy is correct and
slower; T20 measures it on the first machine. Host to device and device to host are one copy.

## T39: the pointers a kernel receives

A node's `KernelPointers` table binds each captured buffer at `Node::execute()`: the home address
on the home device, the resolved instance elsewhere. Ruling under the owner's delegation: a kernel
that captures `buffer.data()` in its own closure cannot be rebound, so such a node is fixed to its
home device and refused elsewhere; a bind that finds no instance is an error, not a silent home
address. T44 carries both and moves the dynamic `DeviceSlot` path onto the table.

## T42: the integrated GPU's pool

An integrated device names the host pool and plans within the smaller of its memory and the host's
usable bytes. Integration is read from `host_unified_memory`. Whether the HIP adapter answers it for
gfx1036 is checked in the closing run on the first machine; if it answers false, the pool is
counted twice and T43's probe takes the fact from the device instead.
