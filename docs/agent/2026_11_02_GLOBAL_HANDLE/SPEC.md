# Global handle: specification

**Status:** Open — planned in `PLAN.md`; no task started
**Implements:** GLOBAL_HANDLE#H0, GLOBAL_HANDLE#H1, GLOBAL_HANDLE#H2, GLOBAL_HANDLE#H3, GLOBAL_HANDLE#H4, GLOBAL_HANDLE#H5, GLOBAL_HANDLE#H6, GLOBAL_HANDLE#H7, GLOBAL_HANDLE#H8, GLOBAL_HANDLE#H9
**Designs:** LAYERED_EXECUTION, GRAPH_REPLAY

## Goal

One job runs across every device of a machine, and later of a cluster, through one identity per
buffer, with the memory of every space added together.
<!-- follows: one-home-per-byte -->

The work covers SushiRuntime and SushiBLAS, and repairs the SushiEngine runtime adapter that H1
breaks. It builds the backend and the surface a consumer records against: file-backed buffers,
declared node costs, splittable nodes and the partitioner. It builds no inference engine. SushiAI
and SushiEngine are consumers at the same level, and both adopt the handle in later work.

The first machine is a Ryzen 7600X with its integrated GPU, an RTX 3080 Ti and the CPU. The runtime
slices a recorded graph on the host, sizes each device's share by its measured speed and capacity,
and the output bytes are equal in every run.

The shape, the reasons and the public API changes are in
[the design](../../design/GLOBAL_HANDLE/README.md). This file states what each phase must prove.

## Constraints

- Every change builds and runs on intel-llvm and on AdaptiveCpp.
- `foundation/handle` has no SYCL include and no dependency.
- No unit depends on a concrete class of another module; `engine/task` reaches residency through
  `HandleResolver` only.
- A new memory space, transfer path or device-key source adds a file and edits no sibling.
- Determinism, as decision 5 of the design states it, holds at any worker count.
- The hot path holds no lock.
- Reserves are measured at start, never assumed.

## Acceptance, one criterion per phase

| Phase | Done when |
| --- | --- |
| H0 | On a machine where one device is visible to two backends, discovery lists it once, and its `DeviceKey` is equal across ten process starts with the device selector reordered; the integrated-GPU probe on the first machine reports pass or fail for each step: HIP SDK 6.x lists gfx1036, `ur_adapter_hip.dll` builds, a kernel compiles for `gfx1036`, `sycl-ls` lists the device, and the kernel's bytes equal the CPU's. |
| H1 | No `void*` identity remains in `foundation/region`, `engine/region_boundary`, `engine/task` or live IO; a graph with ten buffers submits in the same order across 100 runs (closes KI-23); the anchor rule exists once (closes KI-24). |
| H2 | `USMRegistry` and `BufferRegistry` are gone; a test resolves a stale handle and gets `ErrorCode::StaleHandle`; a benchmark shows resolve without a mutex at eight threads. |
| H3 | A node that reads a buffer written on device 0 runs on device 1 with a planned copy, and a write on device 1 invalidates device 0's instance; a capacity test evicts in graph order and the eviction plan hashes equal across runs. |
| H4 | A buffer written on the RTX 3080 Ti is read on the integrated GPU through pinned host memory, and the bytes match; the peer path runs on the software device in CI and the report says which path each copy took; a file-backed buffer larger than the 3080 Ti's memory streams onto the devices while the process's resident host memory stays under the staging ring plus 512 MB. |
| H5 | A replayed step runs no planner code (counted), and directory size stays flat over 10⁵ steps with horizons on. |
| H6 | `TASK_DISPATCH` at wire version 5 carries `GlobalHandle` with golden bytes; a worker killed mid-run is recovered by re-running the producer and the run's output bytes equal an undisturbed run. |
| H7 | SushiBLAS builds against the handle and declares splits on its matrix products, and SushiEngine's runtime adapter converts through the handle, with both suites green; SushiAI's build is run unchanged and the report names each break; a file-backed graph of SushiBLAS products, larger than any one device's memory, runs across the 3080 Ti, the integrated GPU and the CPU with no part read from the file after loading, and gives equal output bytes over ten runs. |
| H8 | A conformance kernel set (sum, dot, gemm, softmax, layer norm) gives equal bytes on the 3080 Ti, the integrated GPU and the CPU, or the report names each operation that differs and why. |
| H9 | The runtime slices any recorded graph from declared node costs and a measured device profile, splitting a declared-splittable matrix product across devices when the cost model says it shortens the step: on the H7 machine the per-device step times differ by at most 10%, the assignment hashes equal across runs, a job that does not fit is refused before loading with the shortfall per space, and after one device is throttled the journal shows its slices moved within two horizons, with a replay of the journal giving the same placement and output bytes. |

## Tests

- A determinism conformance suite extends BB-3's: handle values, plan hash and output bytes are
  compared across runs and across worker counts {1, 2, max}.
- Coherence is tested against a reference model: a sequential interpreter of the same graph that
  keeps one copy of each buffer. Every randomized graph (seeded, sizes, devices, accesses) must give
  the same bytes on the planner as on the reference.
- The partitioner is tested against an exhaustive search on small graphs: its assignment is within
  10% of the best step time the search finds.
- Transfer paths run on a software device in CI and on the devices of the first machine.

## Out of scope

- An inference engine, and any change to SushiAI.
- SushiEngine's own `ResourceID` and `hazard_core`; only its runtime adapter changes.
- A kernel that dereferences remote memory.
- Splitting a node that declares no split; the runtime never guesses how a kernel divides.

## Risks

- SYCL may not reach the 7600X's integrated GPU on Windows. The Unified Runtime HIP adapter compiles
  on Windows, but no one has published a run of it, gfx1036 is not on AMD's Windows support list,
  and the compiler must find the HIP SDK's device libraries. If the H0 probe fails, AdaptiveCpp is
  tried; if that fails too, H4, H7 and H8 run on the 3080 Ti and the CPU, and the report says so.
- The integrated GPU carves its memory from RAM, so it adds a device and no capacity.
- H1 and the `NodeCost` change break SushiRuntime's public surface, SushiBLAS and SushiEngine's
  adapter at once; they land behind one release, which the owner tags. If they break SushiAI's
  build, the report names each break and the backlog carries it.
