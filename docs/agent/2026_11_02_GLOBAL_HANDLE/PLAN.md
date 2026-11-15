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

On 2026-11-15 the owner changed two rules and delegated every engineering decision. No task runs a
build or a test: the one build and the one test run per repository come after the last task, as
`build-throughput` boundary 8 says, and the hardware tasks (T20, T33, T35, T36) are written with
the code and measured in that run. The orchestrator also gives the `inline` tasks to subagents
when its own context would not hold their reading; it still reviews and commits each one.

## Jobs

| Job | Files | Waits on | Status |
| --- | --- | --- | --- |
| T01 | `foundation/handle` | | done |
| T02 | `platform/topology` | T01 | done |
| T03 | HIP probe | | done |
| T38 | `cli`, `cmake/gpu`: the HIP backend on Windows | T03 | done |
| T04 | `foundation/placement`, `surface/api` | T02 | done |
| T05 | `platform/memory` directory | T01 | done |
| T06 | `foundation/region`, `engine/region_boundary`, `engine/task`, live IO, `surface/api` | T05 | done |
| T07 | `surface/api` region composer | T06 | done |
| T08 | `engine/scheduler` and `engine/task` placement | T06 | done |
| T09 | `platform/memory` `usm_registry` | T06 | done |
| T10 | `extension/buffer_exchange` | T06 | done |
| T11 | resolve benchmark | T09 | done |
| T12 | `platform/memory` spaces and intervals | T05 | done |
| T13 | `engine/coherence` planner | T08, T12 | done |
| T14 | `engine/coherence` eviction | T13 | done |
| T39 | `engine/task`, `engine/scheduler`: kernels receive the instance resolved on their device | T13 | done |
| T44 | `engine/task`, `surface/api`: nodes whose pointers cannot be rebound stay home; a missing instance is an error; `DeviceSlot` on the table | T39, T37 | done |
| T40 | `engine/task` `node_placement.cpp`: no refusal across SYCL contexts | T13 | done |
| T41 | `platform/memory` directory port; `engine/coherence` on it | T13 | done |
| T42 | `engine/execution`, `platform/device`: integrated GPU shares the host pool | T13 | done |
| T15 | reference interpreter test | T13 | done |
| T16 | determinism suite | T13 | done |
| T17 | `engine/coherence` same-context and peer paths | T13 | done |
| T18 | `engine/coherence` host-staged path | T17 | done |
| T19 | file space and file path | T18 | done |
| T20 | hardware test, 3080 Ti to integrated GPU | T03, T18, T38 | done |
| T21 | copy plan replay | T13 | done |
| T45 | `engine/graph` `PlanCache`, `surface/api` static `Graph`: the coherence hook and copy-plan replay | T21 | done |
| T22 | horizons | T21 | done |
| T23 | `foundation/core` `NodeCost` | T06 | done |
| T37 | `surface/api` `Residency::Planned` | T13 | done |
| T46 | step driver, `platform/memory` retire, `engine/task` placement: host writes force a replan, retired handles free planned instances, planned-only nodes placed | T37, T21 | done |
| T24 | device profile and cost model | T04, T23 | done |
| T43 | `engine/execution`, `surface/api`: profile file loaded at start, the planner priced by it, the device probe | T24, T18 | done |
| T25 | partitioner | T14, T24, T37 | done |
| T48 | host pricing in the cost model, first-start measurement written to the profile file | T43 | done |
| T47 | partitioner in the composer, live intervals, one FNV helper | T25, T43 | todo |
| T26 | partitioner search and floor tests | T25 | done |
| T34 | `Runtime::partition_report()` | T25 | done |
| T35 | hardware test, H9 graphs | T19, T34 | todo |
| T36 | splitting | T25, T30 | running |
| T27 | rebalancing and journal | T22, T34 | todo |
| T28 | wire version 5 | T10 | done |
| T29 | owner per handle and recovery | T28 | done |
| T30 | SushiBLAS handle, cost and split | T23, T25 | done |
| T31 | SushiEngine runtime adapter, SushiAI build | T06 | done |
| T49 | SushiBLAS regions by handle, host handles, slice on its device | T30, T36 | todo |
| T32 | SushiBLAS conformance kernels | T30 | done |
| T33 | SushiBLAS file-backed large test | T19, T27, T30 | todo |

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

### Follow-ups found by T13

| Task | Runs on | Files | Waits on | Done when |
| --- | --- | --- | --- | --- |
| T39 | `opus` `low` | `engine/task` `handle_location.hpp` callers, dispatch in `engine/scheduler` and the task submit path | T13 | A kernel's captured buffer pointer is replaced by the instance resolved on its node's device at dispatch, and a test shows a node on device 1 reading the copied instance |
| T40 | `sonnet` `medium` | `engine/task/source/node_placement.cpp` | T13 | A node stated on a device in another SYCL context than its anchor's owner is placed, with the copy planned, instead of refused |
| T41 | `sonnet` `medium` | `platform/memory` `ResidencyView` port; `engine/coherence` planner and node factory | T13 | The planner and the node factory depend on the port, not on `ResidencyDirectory` |
| T43 | `sonnet` `medium` | `engine/execution` runtime context and `RuntimeConfig`, `surface/api/source/region_coherence.cpp`, a device `ProfileProbe` | T24, T18 | The eviction policy reaches the composer's hook through `RuntimeContext`, the pages T21 made false are right, a runtime given a profile file prices copies through `CostModel`, a runtime without one keeps the uniform cost, and `ProfileMeasurement` runs on a device through the real probe; a caller installs a `TransferObserver` through `AdvancedControls`, and T20 asserts the path of each copy through it |
| T45 | `opus` `low` | `engine/graph` `PlanCache`, `surface/api` static `Graph` | T21 | A static `Graph` with nodes on two devices gets its copies planned at compile, keeps the plan with the compiled graph and replays it with no planner call, as a `DynamicGraph` does |
| T46 | `opus` `low` | the dynamic and static step drivers, the directory's retire path, `engine/task` `placement_of` | T37, T21 | A `write_range()` after a steady plan forces a replan before the next run; retiring a handle frees every instance the planner made for it and the file space a file-backed buffer added; a node using only planned buffers is placed on the default device until the partitioner gives one; both step drivers call `retire_coherence_history()` at a horizon every N steps; `platform/memory` README states the coherence map, `retire_history()` and `retire_coherence_history()` truly, and `tests/platform/memory` README lists T22's tests |
| T48 | `opus` `low` | `engine/coherence` cost model and resolver, `engine/execution` runtime start | T43 | With a profile loaded, a copy through the host is priced from the host links the profile measured, never infinite; a runtime with no profile file measures each device with `DeviceProfileProbe` on first start and writes the file the next start reads |
| T47 | `opus` `low` | `surface/api` `region_coherence`, `plan_cache`, `engine/coherence` partition model and hashes | T25, T43 | The composer and the static graph build a `PartitionProblem`, give each planned buffer its home from the assignment and refuse an unfitting job before loading; pool demand counts live intervals; `plan_hash` and `assignment_hash` share one FNV helper; the runtime holds the last assignment so `partition_report()` has its rows, the report carries measured time per device, the measured step, the floor's modelled time and each copy's path, and a horizon calls `update_profile_file()` so a second run reads what the first measured |
| T44 | `opus` `low` | `engine/task` `kernel_pointers`, node placement metadata, `surface/api` recorder `DeviceSlot` path | T39, T37 | A node whose kernel captures raw addresses is marked fixed to its home device and refused elsewhere with `InvalidGraph`; a bind with no instance on the node's device fails its verify; the dynamic `DeviceSlot` path binds through `KernelPointers` |
| T42 | `sonnet` `low` | `engine/execution/source/runtime_memory_spaces.cpp`, `platform/device` | T13 | An integrated GPU reports the host pool as its shared pool, and `usable_pool_bytes` counts it once |

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
| T36 | inline | `engine/coherence`: splitting a declared-splittable node into parts sized by device speed, at most one per device; `NodeSplit::slice` receives the part's device queue and the pointers bound on that device (ruling of 2026-11-15) | T25, T30 | A split product's measured step on the first machine is shorter than with splitting turned off, and a node that fits no pool whole runs split |
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
| T49 | `sonnet` `medium` | SushiBLAS worktree `D:/Projects/.worktrees/sushiblas-global_handle`: `tensor.hpp`, `scratch.hpp`, `reduce.hpp`, `counting_sort.cpp`, `prefix_scan.cpp`, host blocks, `gemm_split.hpp` | T30, T36 | Every `BufferRegion` names a handle, a host tensor has a minted host handle, and the GEMM slice runs on the queue and pointers the runtime gives its part |
| T32 | `sonnet` `medium` | SushiBLAS tests: the conformance kernel set (sum, dot, gemm, softmax, layer norm) on every device | T30 | Each operation gives equal bytes on the 3080 Ti, the integrated GPU and the CPU, or the report names each difference and why |
| T33 | inline | SushiBLAS large test: a file-backed product graph larger than any one device's memory | T19, T27, T30 | On the first machine it runs across every device the plan gives a node, with no file reads after loading, equal bytes over ten runs, and a step time at most 1.05 times that of the floor plan's devices alone |

## Closing

The work closes when T33 passes, every phase is moved with `sr design phase-state`, and
`REPORT.md` holds one `dispatch:` line per dispatch and the probe table of T03. Merging to `main`
waits for the owner; the release tag is the owner's.

## Worker rules

The orchestrator hands each worker one task of this plan. These rules come with every
task; the dispatch names the task, its files and its acceptance criterion.

### Start

1. Your worktree starts on an old commit. Run `git reset --hard feature/global_handle` in it before
   reading anything, and confirm `git log --oneline -1` shows the branch head.
2. Read `SPEC.md`, your task's row in `PLAN.md`, `docs/design/GLOBAL_HANDLE/README.md` and, for
   partitioning tasks, `docs/design/GLOBAL_HANDLE/PARTITIONING.md`.
3. Run `sr design owner <path>` for each module you change, and paste the output.

### While working

- No build, no test run, no `sr build`, `sr test`, cmake, ninja or ctest. The owner ordered one build
  and one test run per repository after the whole plan.
- Syntax-check C++ with one `clang++ -fsyntax-only` process at a time, on few files, never in
  parallel. Take flags from `build/compile_commands.json` (`-std=gnu++17`, the include paths of the
  module and its dependencies). The compiler is
  `D:/Projects/sushistack/dependencies/toolchains/llvm-sycl/bin/clang++.exe`; add `-fsycl` for files
  that include SYCL.
- The code is C++17 and must also compile on AdaptiveCpp: no Intel-only extension, no
  `sycl::ext::oneapi` without an existing seam that hides it.
- SushiRuntime knows no math. It orchestrates; kernels and numerics belong to consumers.
- One responsibility per unit; a new memory space, transfer path, device-key source or eviction
  policy adds a file and edits no sibling. Depend on interfaces, never on another module's concrete
  class. The hot path takes no lock.
- Determinism: handles, keys, plans and orders are pure functions of their inputs. No clock, no
  address order and no hash-table iteration order decides an outcome.
- Names are not abbreviated (`Statistics`, `Execution`); acronyms stay in capitals inside names
  (`API`, `UUID`, `PCI`, `ID`).
- Doxygen comments in plain, short English. Line separator comments (`// ===`, `// ---`) are
  forbidden. Load `cpp-code-style`, `source-comments`, `sushisystems-licence`, `testing` and, for a
  README or a page, `documentation`.
- A module that includes another module lists it in `DEPENDS`, and an edge between two modules of one tier is added to `SUSHIRUNTIME_SAME_TIER_EDGES` in `cmake/SushiRuntimeTiers.cmake` (`tools/layering/check_layering.py`).
- Every test suite name opens with `Small_`, `Medium_` or `Large_` (`tools/testing/check_test_sizes.py`).
- Every header and source file gets its mirror test under `tests/` (the commit hook runs
  `tools/testing/check_test_mirror.py`). Register each test file in its tests `CMakeLists.txt`.
- Write licence blocks by copying one from a sibling file of the same language.

### Before reporting

Run and paste: `python tools/layering/check_layering.py .`, `python tools/testing/check_test_sizes.py .`, `python tools/testing/check_test_mirror.py .`,
`python tools/documentation/check_source_comments.py .`,
`python tools/documentation/check_docs_truth.py .`, and each syntax check. `git add -A` your
changes in your worktree. Do not commit.
