# Usability refactor: plan

**Status:** Open. Phase A closed on 2026-10-09; phase B is being specified.

The findings this plan closes are in `REPORT.md` in this folder and are cited by their IDs.
The work lands on `feature/usability_refactor`. Breaking changes are allowed in this pass; the
release that carries them is `v0.4.0`.

## Rulings

Given by the owner on 2026-10-09.

| Question | Ruling |
| --- | --- |
| Layout | More than one module: `modules/<tier>/<module>/`, each with its own `include/`, `source/`, `tests/`, `README.md` and build target |
| `TaskGraph` construction | Stays lock-free. Epoch reclamation and the dependency tracker stay |
| Distributed layer | Trusted LAN with authentication: every DIST finding, a shared secret in HELLO, no encryption, no new dependency |
| Public surface | Anything wrong is broken now, in one MINOR |
| Tier table | Approved as proposed below, subject to what the code shows when phase B opens |
| Numeric primitives | Leave the runtime for SushiBLAS; see below |
| Run mode | The orchestrator's choice: waves where file sets are disjoint, inline otherwise |

Given on 2026-10-10.

| Question | Ruling |
| --- | --- |
| `CMakePresets.json` | Deleted |
| `set(SR_INSTALL ON)` in SushiBLAS, SushiAI and SushiEngine | Added, the same line in each `cmake/Runtime.cmake`, with the CMake groundwork (B6) |
| Other sessions during the physical move | Nobody builds against this tree while the move runs; no worktree |
| Order | The physical move stays where the spec puts it, after the cycles are closed |

## Numeric primitives

Ruled on 2026-10-09: the runtime keeps no mathematics; all of it goes to SushiBLAS, and the
move is part of this work.

Measured the same day: SushiBLAS has its own reductions (`modules/operations/reductions`,
`support/reduce_1d.hpp`) and does not call the runtime's. It has no prefix scan and no counting
sort. SushiEngine calls `add_reduce`, `add_prefix_scan` and `add_counting_sort` from
`matter_device_neighbours.hpp` and does not depend on SushiBLAS today.

The move follows the tier order of the repositories, lowest first, with no repository
depending on unreleased work of another:

1. SushiRuntime, phase C: the four primitives and `Sum`, `Minimum`, `Maximum` leave `Graph`.
   What they need from the runtime (late count, device-sized dispatch, the fault record)
   becomes public recorder interface, with no arithmetic in it. The runtime's tests keep the
   orchestration half, rewritten against a trivial kernel. `v0.4.0` ships without the
   primitives.
2. SushiBLAS: a module receives the primitives, built on that public interface, with the
   fourteen algorithm tests and the bit-equality tests. SushiBLAS releases.
3. SushiEngine: upgrades the runtime, takes SushiBLAS as a dependency and switches
   `matter_device_neighbours.hpp` to it, in one upgrade commit per dependency.

## Constraints measured in the repositories above

| Repository | What it includes from the runtime |
| --- | --- |
| SushiBLAS | `graph/task_types.hpp` (72 files), the umbrella (16), `core/resource_region.hpp` (12), `core/span.hpp`, `graph/task_graph.hpp`, `execution/execution_engine.hpp`, `graph/node.hpp`; calls `live_usm_count`, `get_queue`, `wait_all`, `device_count`, `free_usm` and the three `aligned_malloc_*_on` on `RuntimeContext`; consumes the tree with `add_subdirectory` |
| SushiAI | `core/span.hpp` (138 files), `graph/task_graph.hpp`, `execution/execution_engine.hpp`, `execution/runtime_context.hpp`, `core/error.hpp` |
| SushiEngine | the umbrella, `core/span.hpp`, `interop/interop.hpp`, `core/run_report.hpp`, `core/dispatch_width.hpp`; the three numeric primitives |

The include path stays `<SushiRuntime/<module>/<name>.hpp>`. A header that changes module gets
a forwarding header at the old path, marked deprecated, for `v0.4.x`.

## Tier table

Lowest first. A module includes modules of lower tiers, and of its own tier only where the
table lists the edge.

| Tier | Module | Holds | Comes from |
| --- | --- | --- | --- |
| foundation | `core` | bit, span, export, common, delegate, ref_counted, sushi_ptr, strong_id, result, error, version | `core/`, `version.hpp` |
| foundation | `logging` | logger with a runtime level and sink | `core/logger.hpp` |
| foundation | `sycl` | every toolchain fork behind one header | `core/sycl_compat.hpp`, EXEC-14 |
| platform | `topology` | hwloc provider, discovery, NUMA map, affinity | `topology/` |
| platform | `memory` | allocation, USM registry and allocator, resource region and liveness, epoch reclamation | `memory/`, parts of `core/`, the allocation members of `RuntimeContext` |
| platform | `device` | device inventory and the queue families | the queue members of `RuntimeContext` |
| engine | `observability` | run report, profiling ledger, event timestamps, dispatch width, transfer counters | parts of `core/` |
| engine | `task` | `Task`, `Node`, task types, late bindings | `scheduler/task.hpp`, `graph/node.hpp`, `graph/task_types.hpp` |
| engine | `scheduler` | work-stealing pool, event polling, idle policy, rebalancer | `scheduler/` |
| engine | `graph` | `TaskGraph`, dependency tracker | `graph/` |
| engine | `execution` | runtime context, execution engine, chain plan and run | `execution/` |
| surface | `interop` | external memory vocabulary and import | `interop/` |
| surface | `api` | `Runtime`, `Graph`, `DynamicGraph`, `Buffer`, `State`, vocabulary | `api/` |
| extension | `distributed` | protocol, transports, coordinator, worker | `distributed/` |

Edges inside the engine tier, to be listed in `cmake/`: `scheduler -> task`, `graph -> task`,
`execution -> scheduler`, `execution -> graph`. The five include cycles of LAYOUT-01 close as
follows: the device and memory bricks leave `RuntimeContext` for the platform tier, so the
scheduler and `usm_allocation_owner` stop including `execution`; `Task` and `Node` meet in
`task`, so `scheduler` and `graph` stop including each other; the `Buffer`-typed interop
overloads move to `api`. The distributed hooks in `task`, `scheduler` and `execution` go behind
one offload seam declared in `task`, which `distributed` implements.

The rest of the tree follows `repository-layout`: `applications/examples/`, `.config/doxygen`,
`.config/docker`, `tests/` for cross-module tests, benchmarks and the package consumer,
`SECURITY.md` and `CODE_OF_CONDUCT.md` under `.github/`.

## Phases

Hierarchy first: a phase starts when the one before it has built and passed. Each phase opens
with its wave table (tasks, files, what each waits on, acceptance) appended here.

| Phase | Closes | Breaks |
| --- | --- | --- |
| A. Safety net | TEST-01 to 04, 07, 12, 14, 20; EXEC-01; CORE-01; GRAPHAPI-17; LAYOUT-14 | nothing |
| B. Layout and build | LAYOUT-01 to 28; CORE-11, 12, 19; EXEC-09, 10, 13, 24; OPS-19; TEST-05, 09, 10, 11, 17, 19 | include paths of moved headers (forwarders kept), installed header set, target options |
| C. Public API | API-01 to 22; GRAPHAPI-01 to 23; EXEC-03 to 07, 11, 12, 22; CORE-09, 13, 14, 17; TEST-06, 08 | `Runtime::create`, `RuntimeContext`, allocation entry points, error types, names |
| D. Distributed | DIST-01 to 25; TEST-13, 21 | wire protocol v2, `ITransport`, `RemoteKernel`, default bind address |
| E. Hot path | CORE-03 to 08, 15, 16, 21; EXEC-08, 15 to 18; GRAPHAPI-13, 16, 18, 19; TEST-16 | `Node` layout |
| F. Command line and CI | OPS-01 to 27 | option spellings (aliases kept) |
| G. Documentation and release | DOCS-01 to 22; EXEC-19, 20, 23, 25; CORE-18; API-17, 18; GRAPHAPI-20, 21; `v0.4.0` proposal | nothing |

Phase E changes nothing without a before and an after from the benchmarks phase A repairs.

## Phase A wave

Baseline: `sr build --distributed` and `sr test` on `bfbc137`, recorded in `REPORT.md` when the
phase closes.

| Task | Files | Acceptance | Runs |
| --- | --- | --- | --- |
| A1 cancellation gate | `tests/functional/integration/test_cancellation.cpp` | The cancel is sent after the predicate has run once; no fixed sleep decides the outcome | worker |
| A2 device probe | `tests/common/test_helpers.hpp`, the three distributed test files, `test_distributed_offload_perf.cpp`, `test_graph_compile_perf.cpp` | A test skips only when SYCL reports no device; construction runs outside any catch | worker |
| A3 measurement | `tests/common/measurement.hpp` (new), `test_chain_dispatch_perf.cpp`, `test_root_fanout_perf.cpp`, `test_scheduler_dispatch_perf.cpp`, `test_wide_fanout_perf.cpp`, `test_dynamic_graph_perf.cpp` | Five runs per case; median, minimum and maximum on one `key=value` line with build type and worker count; no allocation inside a timed region | worker |
| A4 layer equivalence | `tests/functional/integration/test_layer_equivalence.cpp` (new) | Byte-equal output for `DynamicGraph` against `Graph`, `add_host` mixed with kernels, `run_async` against `run`, and an `Extent` launch, each under the chain and the forced scheduler layer | worker |
| A5 PCI address | `include/SushiRuntime/topology/hw_discovery.hpp`, `src/topology/hw_discovery.cpp`, `src/topology/numa_mapper.cpp` | No field of `HardwareNode` is read uninitialised; a device without a PCI address skips the lookup and logs one line | worker |
| A6 node name | `include/SushiRuntime/graph/node.hpp`, `include/SushiRuntime/scheduler/task.hpp`, `src/scheduler/scheduler.cpp` | A failing node is logged with its `metadata.name` and registration index | worker |
| A7 build files | `tests/CMakeLists.txt`, `src/CMakeLists.txt`, `tests/functional/integration/test_graph.cpp`, `graph.hpp:2106-2111`, `dynamic_graph.hpp:422-427` | Per-test timeout set; A4 registered; the stale `usm_allocator.cpp` entry gone; `fan_out_success` atomic; `unwrap` moves the report | orchestrator |

The orchestrator closes the wave with `sr build --distributed` and `sr test`.

### Phase A close

Machine: Windows 10, Ryzen 5 7600X (12 workers), RTX 3080 Ti, intel-llvm, Release, distributed
layer on.

| Run | Functional suite | Benchmark suite |
| --- | --- | --- |
| Baseline, `bfbc137` | 368 of 368 passed, 2 skipped, 217 s | not run |
| After the wave | 377 of 377 passed, 3 skipped, 155 s | 19 of 19 passed, 2 skipped, 130 s |

The third skip is `Integration_LayerEquivalence.UntrackedOffsetLaunchesMatchAcrossLayers`, which
found a defect: an untracked kernel can be routed to a device that does not own its buffers. It
is recorded in `docs/reference/KNOWN_ISSUES.md` and `docs/design/REMAINING_WORK.md` and is fixed
in phase C with the routing of region-less tasks.

Two things the workers' reports did not survive. A6 rebuilt the failing task's exception by
rethrowing inside a helper and wrapping it with `std::throw_with_nested`; five existing
failure-path tests then crashed with 0xC0000006 on this toolchain. The orchestrator replaced it
with one overload per exception family and a plain catch ladder, so the original exception is no
longer nested inside the reported one. A6 also removed `Task(const char*)`, which two helper
tasks in `tests/common/test_helpers.hpp` (A2's file) used; the orchestrator adapted them.

The first numbers from the repaired benchmarks, five runs each, median (minimum to maximum).
They are the "before" of phase E.

| Benchmark | Case | Size | Figure |
| --- | --- | --- | --- |
| `scheduler_dispatch` | inline baseline | 500 000 tasks | 1.68 ns per task (1.39 to 1.85) |
| `scheduler_dispatch` | scheduler | 500 000 tasks | 148 ns per task (137 to 159) |
| `wide_fanout` | 20 000 steps of 64 tasks | 1 280 000 tasks | 547 ns per task (385 to 666) |
| `root_fanout` | batched submit | 1 root | 5 718 ns per root and step (5 440 to 5 919) |
| `root_fanout` | batched submit | 1 024 roots | 6 053 ns per root and step (5 843 to 6 069) |
| `chain_dispatch` | scheduler layer | 1 node | 105 136 ns per node and step (66 077 to 134 570) |
| `chain_dispatch` | scheduler layer | 16 nodes | 36 800 ns per node and step (30 790 to 46 183) |
| `chain_dispatch` | scheduler layer | 256 nodes | 38 364 ns per node and step (15 243 to 41 482) |
| `chain_dispatch` | scheduler layer | 1 024 nodes | 25 973 ns per node and step (13 890 to 41 355) |
| `chain_dispatch` | chain layer | 1 to 1 024 nodes | 5 803 to 6 022 ns per node and step |

A dependent node costs 26 to 105 microseconds through the scheduler layer of `Graph::run` and
about 6 through the chain layer, while a bare task through the scheduler costs 148 nanoseconds.
The scheduler-layer figure also moves by a factor of two to three between runs.

## Phase B progress

Each row is closed by `sr build --distributed` and `sr test` on the machine of phase A.

| Step | Commits | Functional suite | Benchmark suite |
| --- | --- | --- | --- |
| B0, dead includes | `94cb591` | 377 of 377 | not run |
| B1, pure moves and the four bricks | `0774396` to `414c051` | 414 of 414 passed, 3 skipped | not run |
| B2, `CompiledGraph` to graph, seam headers, `RuntimeContext` on the bricks | `f53630b`, `d305f03`, `23719da` | 414 of 414 passed, 3 skipped | 19 of 19 passed |
| Step 5, scheduler off the context | `159d09b` | 414 of 414 passed, 3 skipped | 19 of 19 passed; scheduler 141 ns per task (130 to 156), root fan-out 5 394 to 6 202 ns, chain layer 5 856 to 6 557 ns, wide fan-out 898 ns (442 to 1 321) |
| Step 6, offload state on the seam type | `5281030`, `78b445c` | 414 of 414 passed, 3 skipped | not run; the step adds nothing to a node without a coordinator |
| Step 7, control path on the seam | `refactor(scheduler)!` after `78b445c` | not built: the owner ruled on 2026-10-10 that the steps from here are built and tested once, when the refactor is complete. Syntax check of 120 units passed, flag-off check of 9 units passed | not run |

Benchmarks after B2, median of five, two invocations of the suite a few minutes apart:

| Benchmark | Case | Phase A | B2, first run | B2, second run |
| --- | --- | --- | --- | --- |
| `scheduler_dispatch` | scheduler, 500 000 tasks | 148 ns | 153 ns | not rerun |
| `wide_fanout` | 1 280 000 tasks | 547 ns | 366 ns | not rerun |
| `root_fanout` | 1 root | 5 718 ns | 7 920 ns | 5 841 ns |
| `root_fanout` | 1 024 roots | 6 053 ns | 7 385 ns | 6 303 ns |
| `chain_dispatch` | chain layer, 1 node | about 5 900 ns | 8 532 ns | 7 113 ns |
| `chain_dispatch` | chain layer, 1 024 nodes | about 5 900 ns | 7 111 ns | 6 098 ns |
| `chain_dispatch` | scheduler layer, 1 node | 105 136 ns | 447 562 ns | 66 853 ns |
| `chain_dispatch` | scheduler layer, 1 024 nodes | 25 973 ns | 18 888 ns | 17 589 ns |

The two B2 runs differ from each other by up to 35 percent on the kernel-submitting cases, more
than either differs from phase A, so they show no regression and cannot rule out one of a few
percent. The machine had 3 GB of 15 free and other sessions running. Phase E needs a quiet
machine and more than one invocation per figure before it claims a gain.

The first closing run of B2 was stopped for lack of memory at 12 parallel compiles; the run
that passed set `CMAKE_BUILD_PARALLEL_LEVEL=4`. `sr build` has no flag for the job count (phase F).

## Defects found on the way, for phase D

Found by workers while reading the coordinator and the scheduler; none is fixed yet.

- The coordinator leaks the `Node*` entries still pending when it stops.
- `try_offload` does not check for a stopped coordinator, and touches the node after publishing it to the pending map, where the transport thread can already complete it.
- A dynamic `State` node bumps the version of the write list snapshotted when it was added, not of the live list, so after a swap the bump lands on the other buffer.
- A node skipped in an aborted run still bumps the versions of the buffers it would have written, and can be sent to a worker once more before it is skipped.
- `try_offload` copies the two baked handle vectors on every dispatch.
- A run hangs when the task deadline is 0 and a live worker never answers.
- The flag-off build of steps 5 and 6 passed a syntax check only; it is built and tested once step 8 has removed the flag below `distributed`.