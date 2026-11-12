# Global handle: plan

The tasks that carry out [the specification](SPEC.md), in hierarchy order. Each task has one
acceptance criterion and one build.

## How it runs

The owner chose two subagents and inline work, running asynchronously, with no waves.

- **Two worker slots.** At most two workers run at once. When a slot frees, the orchestrator
  dispatches the next task whose `Waits on` tasks are committed. Nothing waits for a wave to close.
- **Inline in between.** The orchestrator takes the tasks marked `inline`: public interfaces, data
  ownership and the planners. It works on them while workers run.
- **Isolation.** Each worker runs in its own git worktree, branched from the latest commit of
  `feature/global_handle`, so a task starts only from committed dependencies. Two running tasks
  never hold the same file.
- **Builds.** Workers run no build; they syntax-check with one `clang++` at a time, on few files.
  The orchestrator merges a worker's branch, runs the task's build through `sr` with at most 4 jobs,
  one build at a time, and commits. A red build goes back to the worker's report and the ladder in
  `model-routing`.
- **AdaptiveCpp.** Every dispatch carries the rule that the change must build on AdaptiveCpp. The
  AdaptiveCpp lane runs once when a phase closes, on WSL2, since AdaptiveCpp is not installed on
  Windows.
- **Documentation and state.** The orchestrator writes module READMEs, the changelog, the backlog
  and `sr design phase-state` in the commit that closes each task. Workers report false pages.
- **Branches.** `feature/global_handle` in SushiRuntime, SushiBLAS and SushiEngine. SushiAI gets no
  branch; T31 runs its build unchanged.

Model per task follows `model-routing`; `opus` at `high` is not used.

## Tasks

`inline` is the orchestrator (`opus` `medium`). Paths are under `modules/` unless a repository is
named.

### H0: device keys

| Task | Runs on | Files | Waits on | Done when |
| --- | --- | --- | --- | --- |
| T01 | inline | new `foundation/handle`: `global_handle.hpp`, `device_key.hpp`, `handle_space.hpp`, `handle_resolver.hpp`, CMake, README; its tests | none | `GlobalHandle` and `DeviceKey` tests pass: bit layout, order, hash, and the module has no SYCL include |
| T02 | `sonnet` `medium` | `platform/topology`: one `DeviceKeySource` per source, `hw_discovery.cpp` split; its tests | T01 | A fake topology with one card under two backends is listed once, and keys are equal over ten starts with the selector reordered |
| T03 | owner, then inline | HIP SDK 6.x install (owner); `ur_adapter_hip.dll` built the way the CUDA adapter under `D:\Projects\.sushisystems\ur\` was built; a probe kernel | none | `REPORT.md` holds pass or fail for each of the five probe steps the spec names; on fail, the AdaptiveCpp route is tried and recorded |
| T04 | `sonnet` `low` | `foundation/placement`, `surface/api`: `Runtime::device_key()`, `Runtime::device_index(DeviceKey)`, rules naming a `DeviceKey` | T02 | A placement rule naming a `DeviceKey` resolves to the same device with the selector reordered (closes KI-25) |

### H2 then H1: identity

The directory is built before the identity switches to it, so H1 has a minter to call.

| Task | Runs on | Files | Waits on | Done when |
| --- | --- | --- | --- | --- |
| T05 | inline | `platform/memory`: `residency_directory.hpp/.cpp`, chunked table on epoch reclamation, implementing `HandleSpace` and `HandleResolver`; its tests | T01 | Mint, resolve and retire pass, and a stale handle returns `ErrorCode::StaleHandle` |
| T06 | inline | `foundation/region`, `engine/region_boundary`, `engine/task`, live IO, `surface/api` `Buffer`: `BufferRegion::base` becomes `GlobalHandle`, `Buffer::handle()` | T05 | The full runtime suite is green with no `void*` identity left in those modules |
| T07 | `haiku` `high` | `surface/api/source/region_composer.cpp`, a submission-order test | T06 | A graph with ten buffers submits in the same order across 100 runs (closes KI-23) |
| T08 | `sonnet` `low` | `engine/scheduler/source/dispatch_placement.cpp`, `engine/task/source/node_placement.cpp`; its test | T06 | The anchor rule exists once and both callers agree on a test that disagreed before (closes KI-24) |
| T09 | `haiku` `high` | `platform/memory` `usm_registry.*` and its callers | T06 | `USMRegistry` is gone and the suite is green |
| T10 | `haiku` `high` | `extension/buffer_exchange` `buffer_registry.*` and its callers | T06 | `BufferRegistry` is gone and the distributed suite is green |
| T11 | `haiku` `medium` | a resolve benchmark under `tests/` | T09 | The benchmark resolves at eight threads with no mutex on the path, and its numbers are in `REPORT.md` |

### H3: coherence

| Task | Runs on | Files | Waits on | Done when |
| --- | --- | --- | --- | --- |
| T12 | inline | `platform/memory`: instances per space and the interval coherence map in the directory entry; `MemorySpace` interface and the device and host spaces with measured reserves | T05 | Interval tests pass: split, merge of equal neighbours, invalidation on write |
| T13 | inline | new `engine/coherence`: `CoherencePlanner`, allocate, copy and free nodes, the region composer hook; the span refusal in `dispatch_placement.cpp` removed | T08, T12 | A node reading a buffer written on device 0 runs on device 1 with a planned copy, and the write invalidates device 0's instance |
| T14 | `sonnet` `medium` | `engine/coherence`: `EvictionPolicy` interface, logical-time strategy, pinning; its tests | T13 | A capacity test evicts in graph order and the eviction plan hashes equal across runs |
| T15 | `sonnet` `medium` | a reference sequential interpreter and the seeded randomized coherence test, under `tests/` | T13 | Every seeded graph gives equal bytes on the planner and the reference |
| T16 | `haiku` `high` | the BB-3 determinism suite: handle values, plan hash and bytes across worker counts {1, 2, max} | T13 | The suite passes on the software device |

### H4: transfer paths and files

| Task | Runs on | Files | Waits on | Done when |
| --- | --- | --- | --- | --- |
| T17 | `sonnet` `medium` | `engine/coherence`: `TransferPath` interface, `SameContextPath`, `PeerPath`; tests on the software device | T13 | Both paths copy with matching bytes on the software device, and the report says which path each copy took |
| T18 | `sonnet` `medium` | `engine/coherence`: `HostStagedPath` with the pinned staging ring and overlapping chunks; its tests | T17 | A copy between two spaces with no direct path matches bytes on the software device |
| T19 | inline | `platform/memory` file space, `engine/coherence` `FilePath`, the public buffer created from a file range | T18 | A file-backed buffer larger than the 3080 Ti's memory streams onto the devices with resident host memory under the ring plus 512 MB |
| T20 | inline | a hardware test on the first machine | T03, T18 | A buffer written on the 3080 Ti is read on the integrated GPU through pinned host memory with matching bytes, or the report records the T03 fallback |

### H5: replay and horizons

| Task | Runs on | Files | Waits on | Done when |
| --- | --- | --- | --- | --- |
| T21 | `sonnet` `medium` | the copy plan stored in the compiled graph and replayed through GRAPH_REPLAY; a planner call counter | T13 | A replayed step runs no planner code, by the counter |
| T22 | `haiku` `high` | horizon nodes that retire coherence history; a 10⁵-step test | T21 | Directory size stays flat over 10⁵ steps |

### H9: partitioning

| Task | Runs on | Files | Waits on | Done when |
| --- | --- | --- | --- | --- |
| T23 | inline | `foundation/core` `node_cost.hpp`: read bytes, written bytes, operation kind; the split declaration on `NodeOptions` | T06 | The runtime suite is green and the profiling records carry the new fields |
| T37 | inline | `surface/api`: `Residency::Planned` on `Buffer` and `State`, with no allocation at creation and host access through the directory; its tests | T13 | A planned buffer written by `write_range()` before a plan is read back equal after a node on another device wrote it, a stated device is refused with `InvalidArgument`, and a graph with no planned buffer gives the same placement as before the change |
| T24 | `sonnet` `medium` | `engine/coherence`: `DeviceProfile` measurement, the profile file keyed by `DeviceKey`, `CostModel` | T04, T23 | A profile written once and read back gives the same `CostModel` times, and a changed file changes them |
| T25 | inline | `engine/coherence`: `Partitioner`: rank, earliest-finish placement of whole nodes, cut refinement, capacity per pool, the fill plans over device subsets and the floor check, refusal with the shortfall per pool | T14, T24, T37 | A job that does not fit is refused before loading with the shortfall per pool, assignments hash equal across runs with one profile file, and the fill plan of a chain larger than the fastest pool crosses each device boundary once |
| T26 | `sonnet` `medium` | the exhaustive-search test for small graphs and the floor property test over seeded graphs and profiles | T25 | The partitioner's step time is within 10% of the best the search finds, no chosen plan is modelled slower than its floor plan, and a device a thousand times slower gets no node |
| T34 | `sonnet` `medium` | `surface/api`: `Runtime::partition_report()`; the profile entries folded from the profiling ledger at a horizon | T25 | The report's rows match the plan on the software devices, and a second run reads the throughput the first one measured |
| T35 | inline | a hardware test on the first machine: the three graphs of the H9 acceptance, two against the 3080 Ti alone and the chain against its forced cuts | T19, T34 | Each comparison of the H9 acceptance holds, and the numbers are in `REPORT.md` |

### H10: splitting and rebalancing

| Task | Runs on | Files | Waits on | Done when |
| --- | --- | --- | --- | --- |
| T36 | inline | `engine/coherence`: splitting a declared-splittable node into parts sized by device speed, at most one per device | T25, T30 | A split product's measured step on the first machine is shorter than with splitting turned off, and a node that fits no pool whole runs split |
| T27 | inline | rebalancing at horizons, the measured floor check and the placement journal | T22, T34 | After one device is throttled its nodes move within two horizons; with a doctored profile the partitioner returns to the floor plan within two horizons; a journal replay gives the same placement and bytes |

### H6: distributed

| Task | Runs on | Files | Waits on | Done when |
| --- | --- | --- | --- | --- |
| T28 | `sonnet` `medium` | `extension/wire`, `extension/transport`: wire version 5, `GlobalBufferID` folded into `GlobalHandle`; goldens | T10 | `TASK_DISPATCH` at version 5 carries `GlobalHandle` and matches its golden bytes |
| T29 | `opus` `low` | `extension/offload_coordination`, `extension/distributed`: owner per handle, recovery by re-running the producer | T28 | A worker killed mid-run on the in-process fabric is recovered, and the output bytes equal an undisturbed run |

### H7: consumers, H8: reproducibility

| Task | Runs on | Files | Waits on | Done when |
| --- | --- | --- | --- | --- |
| T30 | `sonnet` `medium` | SushiBLAS: tensors report their handle, matrix products declare `NodeCost` and a row or column split | T23, T25 | The SushiBLAS suite is green against the runtime branch |
| T31 | `haiku` `high` | SushiEngine `modules/foundation/execution/include/SushiEngine/execution/backend/runtime_backend.hpp`: the `ResourceID` to `GlobalHandle` table; then SushiAI's build, unchanged | T06 | The SushiEngine suite is green, and SushiAI's build result is in `REPORT.md` with each break named |
| T32 | `sonnet` `medium` | SushiBLAS tests: the conformance kernel set (sum, dot, gemm, softmax, layer norm) on every device | T30 | Each operation gives equal bytes on the 3080 Ti, the integrated GPU and the CPU, or the report names each difference and why |
| T33 | inline | SushiBLAS large test: a file-backed product graph larger than any one device's memory | T19, T27, T30 | On the first machine it runs across every device the plan gives a node, with no file reads after loading, equal bytes over ten runs, and a step time at most 1.05 times that of the floor plan's devices alone |

## Closing

The work closes when T33 passes, every phase is moved with `sr design phase-state`, and
`REPORT.md` holds one `dispatch:` line per dispatch and the probe table of T03. Merging to `main`
waits for the owner; the release tag is the owner's.
