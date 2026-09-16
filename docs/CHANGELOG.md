# Changelog

All notable changes to SushiRuntime are documented here.

Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) — versions follow [Semantic Versioning](https://semver.org/).

---

## [Unreleased]

### Added
- 2026-09-16 — Added `API::LateCount` and late-bound overloads of `add_reduce` and `add_segmented_reduce`, planned from a capacity and resolved per run (`graph.hpp`, `api/detail/fixed_order_scan.hpp`).
- 2026-09-16 — Added `Graph::add_prefix_scan`, a late-bound exclusive uint32 scan over `add_reduce`'s tile tree (`graph.hpp`).
- 2026-09-16 — Added per-dispatch node event records with a run clock pairing, and derived `NodeTiming` from them (`core/profiling_ledger.hpp`, `event_polling.hpp`, `execution_engine.cpp`).
- 2026-09-16 — Added `Graph::set_profiling_requested`, which routes a graph's device nodes to the profiling queue and gates its timing per graph instead of runtime-wide (`graph.hpp`, `scheduler.cpp`).
- 2026-09-15 — Added `Core::NodeEventRecord`, `Core::ClockPairing` and `RunReport::node_events`/`clock` for per-dispatch node timing (`core/run_report.hpp`).
- 2026-09-15 — Added a registration index on every node, shared by the tile levels of one `add_reduce`, and `TaskGraph::reserve_registration()` (`graph/node.hpp`, `graph/task_graph.hpp`).
- 2026-09-15 — Added a lazily built profiling queue per device on the plain queue's context (`RuntimeContext::profiling_queue`, `runtime_context.cpp`).

## [0.3.0] — 2026-09-15

### Breaking
- 2026-09-14 — Made `Graph::add` throw `Errc::invalid_access` for `API::sized_from_device`/`API::based_at_device` and raw device-pointer counters unless the counter is host USM (`Residency::Host`); `Shared` and `Device` counters that worked before are now rejected (`buffer.hpp`, `dynamic.hpp`, `runtime_context.cpp`).
- 2026-08-25 — Renamed the shared CLI layer's import from `sushicli` to `sushicore` across every import site (`cli/config.toml`, `cli/sushiruntime/config.py`, `cli/sushiruntime/console.py`, `cli/sushiruntime/services/diag.py`).

### Added
- 2026-09-14 — Added `API::Interop` for host memory export and device memory import (`interop/`, `Runtime::interop()`).
- 2026-09-14 — Added `Memory::Allocation`, `IAllocationOwner` and `UsmAllocationOwner`; `Buffer<T>` now owns its memory through an `Allocation` (`memory/allocation.hpp`, `memory/usm_allocation_owner.hpp`, `api/data/buffer.hpp`).
- 2026-09-14 — Added a per-vendor GPU toolkit locator layer (`cmake/gpu/locate/CudaToolkit.cmake`, `IntelToolkit.cmake`, `RocmToolkit.cmake`); `DetectGpu` and `Cuda` now request vendors through it.
- 2026-09-14 — Changed the Windows CUDA locator to fall back to the machine registry when the shell's `CUDA_PATH` predates the install (`cmake/gpu/locate/CudaToolkit.cmake`).
- 2026-09-15 — Added the `sushiruntime_gpu_offload` interface target and its `SushiRuntime::gpu_offload` alias, publishing `-fsycl-targets` and backend options to consumers outside the runtime's own directory (`cmake/gpu/GpuBackend.cmake`).
- 2026-09-14 — Added `Residency::Host` over pinned host USM and `RuntimeContext::malloc_host_on` (`buffer.hpp`, `dynamic.hpp`, `runtime_context.cpp`).
- 2026-08-30 — Added `API::DynamicGraph::find_region(key)`, a const lookup returning the live region or `nullptr` without marking it for rebuild.

### Changed
- **`Memory::UsmRegistry` owns USM pointer ownership.** `RuntimeContext` managed
  queues, held the device registry, allocated USM and tracked which device owned
  each pointer; only the last has an invariant of its own — a pointer is known
  from allocation until it is freed, exactly once. Extracted, it is testable
  without standing up a `Runtime`, which nothing about the previous arrangement
  was. `free_usm` now resolves and forgets in one locked step rather than two, so
  a second free of the same pointer is distinguishable from the first instead of
  reading an owner the first is already acting on. `Unit_UsmRegistry` covers the
  round trip, double-take, and concurrent record/take under ASan.
- **Packaging is verified as a test suite, `sr test --suite package`.** Whether an
  installed runtime works outside its own tree has a pass/fail answer, so it is a
  test rather than a separate verb. Two CTest tests joined by a fixture — one
  installs to a prefix, one builds `tests/package/` against it and runs the result
  with nothing on `PATH` but the prefix — so a failure says which half broke. `sr
  package` keeps only what is genuinely a release action: install and archive.
  Its `--no-verify` flag is gone and its default prefix moved to `build/dist`,
  leaving `build/stage` to the test suite.
- **CMake modules are included by absolute path.** `ProjectOptions.cmake` and
  `Vcpkg.cmake` shared their names with SushiEngine's, and `CMAKE_MODULE_PATH` is
  one flat first-match-wins list that the engine populates before it
  `add_subdirectory`s this tree — so under an engine build `include(ProjectOptions)`
  resolved to the engine's file and `SR_ENABLE_DISTRIBUTED` and `SR_BUILD_TESTS`
  were never defined. Proven by their absence from the engine build tree's cache
  while `SR_DETERMINISTIC_FP`, whose module name is unique, was present. Harmless
  only because both options default to OFF. Every include is now absolute, the two
  colliding modules are prefixed, and `CONTRIBUTING.md` §4 gained the rule, which
  covered target and function names but not module filenames.

### Fixed
- 2026-09-14 — Fixed `sr build`'s TSan message to match `cmake/Sanitizers.cmake`'s Windows exclusion instead of naming icx-cl as the untested case (`cli/sushiruntime/services/project.py`, `docs/ARCHITECTURE.md`).
- 2026-08-18 — Fixed the determinism target needing CMake 3.30: kept `$<CXX_COMPILER_FRONTEND_VARIANT:...>` where it is available and fell back to configure-time frontend detection below 3.30 (`cmake/Determinism.cmake`).

### Removed
- **The pooled USM allocation path.** `BitmaskMemoryPool`, `ThreadLocalMagazine`,
  `USMAllocator`, `IAllocatorStrategy` and `create_allocator` are gone, along with
  `RuntimeContext::get_allocator()`. The runtime had two allocation paths and only
  one was used: `Buffer` and `State` reach `malloc_shared_on` directly, so nothing
  on the public surface ever touched the pool — its only callers were two
  benchmarks.

  The decision rests on a measurement rather than on the pool's 4 KB ceiling.
  Instrumenting every USM entry point shows allocation happens once per
  `Buffer`/`State` the caller constructs, at setup, and never during a run: 1000
  steps allocate nothing, `add_reduce` allocates its partials column once at
  description time, and 32 regions streaming in plus 20 drop/re-add/run cycles
  allocate nothing at all, because a region describes work over buffers the caller
  already owns. Pooling amortises high-frequency small allocation, and there is
  none. The same measurement retires the other suspicion: the process-global mutex
  on the public path is taken a handful of times at startup.

  Deleted rather than moved to an internal header, because moving hides dead code
  instead of removing it. Three smaller debt items go with the files: the bucket
  ceiling, `__builtin_clzll` bypassing the project's own MSVC shim, and
  `LockFreePageMap` masking 50 virtual-address bits, which aliases on LA57 hosts.
  No replacement interface — one implementation with no second one asked for is a
  speculative seam. Design recorded in
  `docs/superpowers/specs/2026-08-18-usm-allocation-seam-design.md`.

  A public API break, taken pre-freeze at 0.2.0 with no out-of-tree consumers;
  SushiEngine referenced none of these symbols.

### Documentation
- 2026-09-07 — Renamed the hub's command from `ss` to `hub` in the manual (`docs/`).

## [0.2.0] — 2026-08-17

### Added
- **An installed SushiRuntime now carries what it needs to run.**
  `tests/package/` had never been exercised. Doing so showed the two halves of
  packaging fail independently: a consumer *built* against an install prefix
  fine — `find_package` resolved, the flag-hygiene assertion held — and then
  died with `0xC0000135` when run with nothing on `PATH` but the prefix.
  `install(TARGETS)` staged `sushiruntime.dll` and nothing else, while the
  library imports the SYCL runtime and `hwloc-15.dll` directly and the SYCL
  runtime loads its Unified Runtime adapters by name afterwards. A developer
  machine has all of them on `PATH` already, so no ordinary build or test run
  could have surfaced it.

  `cmake/SushiRuntimeInstall.cmake` now stages them under a
  `sushiruntime_redist` component, split by kind: direct imports of
  `sushiruntime.dll` warn when missing, while the UR adapters — which vary per
  bundle — are staged when present and skipped silently when not. The SYCL
  runtime is matched by pattern rather than named, after a toolchain upgrade to
  DPC++ 7.1.0 changed `sycl8.dll` to `sycl9.dll` and produced a package that
  installed cleanly and would not start. CPack archives the same install rules,
  so the archive cannot drift from a plain `cmake --install`. Redistribution
  licensing is untouched and stays BB-5d's gate; a local stage is not a
  distribution.

- **Any node can be named, with `name_last()`.** Every profiler-facing consumer
  already built a diagnostic name per node and had nowhere to put it: only the
  offload path looked like it accepted one, and even that hardcoded a literal
  (`"fluent_offload"`). Everything else bottomed out in `TaskGraph::add_node` /
  `add_native`, neither of which touched `Node::metadata`, so every node's
  `RunReport::NodeTiming::name` read `"unnamed_task"` and a profile of a real
  graph was a list of identical labels.

  The name is attached after the call rather than passed to each overload:

  ```cpp
  sim.add(API::Reads(a), API::Writes(b), n, integrate).name_last("integrate");
  ```

  A trailing argument was the first attempt and it does not reach two of the
  launch shapes — `add(Extent, captures..., fn)` and `add(State, States..., fn)`
  end in a parameter pack whose last element is the callable, so a defaulted
  trailing name is swallowed by the pack. Shipping it on the shapes that could
  take one would have left the most common shape of all,
  `add(Reads, Writes, n, fn)`, unnamed, and split the API into two spellings a
  caller has to keep straight. One spelling that works everywhere, including any
  shape added later, is the smaller surface.

  `Graph::name_last` sets the pending name on the recorded task; the name reaches
  the node through `TaskGraph::name_last_node` when the plan is built, so it can
  be attached after `add()` has already returned. `Region` exposes it verbatim.
  A task that expands to several nodes, such as a reduction, names its last one.
  `Integration_Observability.EveryAddShapeCanBeNamed` covers all five shapes.

- **SushiRuntime is consumable out of tree.** `install(EXPORT SushiRuntimeTargets
  NAMESPACE SushiRuntime::)` plus a generated `SushiRuntimeConfig.cmake` and version
  file, so `find_package(SushiRuntime REQUIRED)` and
  `target_link_libraries(... SushiRuntime::SushiRuntime)` work from a project with no
  access to this source tree — the first such build in the project's history. The
  same namespaced target is available in-tree via an `ALIAS`, so a consumer can move
  between `add_subdirectory` and `find_package` without editing its link lines.
  `tests/package/` is a complete working consumer and asserts, from the only side it
  is observable, that the exported interface carries the floating-point contract and
  nothing else — no `-Werror`, no `-march`, no sanitizer selection, and not even the
  in-tree policy targets' names.
- `include/SushiRuntime/version.hpp` — `SUSHIRUNTIME_VERSION_{MAJOR,MINOR,PATCH}` and
  a comparable `SUSHIRUNTIME_VERSION` for compile-time branching, plus
  `runtime_version()` / `runtime_version_string()` compiled into the library and
  `version_matches()` to compare the two. The macros describe the headers you
  compiled against; the accessors describe the binary that loaded. A mismatch —
  typically an older shared library found first on the loader path — otherwise
  surfaces as a crash at an arbitrary later point with nothing pointing at the cause.
  The literals are checked against the CMake project version at configure time, so
  the two statements of the same fact cannot drift.
- `docs/INTEGRATION.md` — the consumer's contract, starting from the fact that decides
  everything else: the runtime's kernels are header templates instantiated in the
  consumer's translation units, so the consumer's SYCL-touching sources must be
  compiled by a SYCL compiler and the consumer's floating-point flags are part of the
  determinism guarantee. Also documents the two-lane build pattern that keeps the
  SYCL-compiled subtree small, what the exported target does and does not carry, and
  the startup version check.
- Co-tenancy contract on `RuntimeConfig` (now an alias of `Execution::RuntimeSettings`,
  a leaf type in the Execution layer): `worker_count`, `first_core` and `pinning`
  (`PinPolicy::Pin` / `Float`) say how much of the machine the runtime may take and
  where. The runtime is no longer the machine's sole tenant by assumption — a host
  with its own render, audio and job threads reserves cores for them and hands the
  rest over. A range that does not fit the machine is clamped and warned about, never
  silently ignored, and `Runtime::advanced().worker_pool()` reports the pool as it was
  actually built rather than as it was requested.
- Caller-owned run reports: `Graph`/`DynamicGraph` gained `run(report)`,
  `run(n, report)`, `try_run(report)`, `try_run(n, report)`, and `RunHandle` gained
  `wait(report)` / `try_wait(report)`. They reset and refill a `RunReport` the caller
  keeps, so a frame loop stops allocating one per tick (the previous per-run
  `numa_statistics` allocation is gone). `Core::RunReport::reset()` is the
  clear-but-keep-capacity primitive behind it. The report is also reset on the
  rejection paths, so a caller that skipped the returned error cannot read the
  previous run's numbers out of a run that never happened. All the run entries share
  one driver, so the reuse forms cannot drift from the allocating ones.
- `HardwareFilterConfig::require_fp64` — admits only devices reporting
  `sycl::aspect::fp64`. A double-precision workload now fails at device selection with
  a precise error naming the active filter, instead of at first kernel launch with a
  driver error. Selecting no device at all is likewise a clear `unsupported` error
  rather than a downstream failure.
- `SR_DETERMINISTIC_FP` (CMake option, default ON) and the
  `sushiruntime_deterministic_fp` interface target: `/fp:precise` under an MSVC-style
  frontend, `-ffp-contract=off -fno-fast-math` (plus `-fp-model=precise` on IntelLLVM)
  otherwise. Linked `PUBLIC`, because header-template kernels are instantiated in the
  consumer's translation units — the floating-point contract has to reach them or the
  determinism the API promises stops at the library boundary. Model and contraction
  are separate axes on `icx-cl`, whose contraction default is `fast` under every
  model but `strict`, so an MSVC-frontend build additionally probes for
  `-ffp-contract=off` (then `/Qfp-contract=off`) and adds whichever the driver
  accepts; `/fp:precise` alone would have left a Windows oneAPI build free to fuse a
  multiply and an add and disagree with the Linux lane.
- `Integration_SteadyStateAllocation` — makes a tick's allocation cost measurable
  and then holds it to a shape. A replacement global operator new, gated by a
  thread-local flag so only the driving thread is observed, records what a steady
  state tick allocates; the suite asserts that the value-returning `run()` costs at
  least one allocation per tick more than `run(report)`, and that the reuse form's
  cost does not scale with tick count. It deliberately does not assert an absolute
  zero — whether a shared library's internal allocations reach a replacement in the
  executable differs between ELF and Windows, so a count is not a portable claim
  while those two relative properties are. It builds as its own executable so the
  replacement does not displace the sanitizer's operator new for the rest of the
  suite.
- `Integration_Determinism` — the determinism conformance suite. The same graph runs
  at worker counts {1, 2, whole machine} with the rebalancer off and every output
  byte must match; the input is built so that any reassociation changes the result,
  and the reduction totals are compared separately so a failure says which half
  moved. It also asserts that no fast-math macro is defined in a translation unit
  outside the library, which is the evidence that the FP contract crossed the
  boundary rather than stopping at the library edge.
- `Core::Delegate<Sig, Capacity>` — a move-only, allocation-free type-erased callable
  that replaces `std::function` for the runtime's stored closures. It holds the target
  in a fixed inline buffer with a hand-written three-entry vtable (invoke / move /
  destroy), carries no RTTI, and never allocates: a capture too large for `Capacity` is
  a compile error, not a silent heap spill. The late-binding `Dynamic` providers and
  the per-step `StepMachinery` hooks now store `Delegate`s, so no per-step heap
  allocation or `std::function` indirection remains on those paths.
- `Buffer::host()` / `State::host()` — a branch-free bulk host view (`span<T>`) over a
  shared buffer: the residency is checked once at acquisition, then the span indexes
  with no further checks. Device-resident handles throw `invalid_access` (see Changed).
- Dangling-resource detection: a `Buffer`/`State` now owns a `Core::Liveness` token and
  a graph weakly observes it for every typed handle a task records. A run that
  references a handle freed beforehand returns a `dangling_resource` error through
  `try_run()` instead of dispatching a kernel against released memory. Raw-pointer
  overloads opt out of tracking by design.
- `DeviceIndex` and `ElementRange` strong types (`api/data/buffer.hpp`): a device
  selector can no longer be confused with an element count, and a transfer/sub-range
  window is one named value instead of a pair of bare `std::size_t` arguments.
- `Runtime::advanced()` returns an `AdvancedControls` facade — a narrow, read-only view
  of internal state (device count, profiling/rebalancer flags, transfer bytes) — so
  callers no longer reach through the whole `RuntimeContext` for these.
- Dynamic region-partitioned graph (`Runtime::dynamic_graph()`): the mutable
  counterpart to the static `graph()`, for streaming, open-world workloads where the
  set of work changes between steps. Work is grouped into regions keyed by a caller id
  (a spatial cell or streaming chunk); `DynamicGraph::region(key)` records tasks with
  the same fluent `add()` surface as a static graph, and `drop(key)` removes a region.
  Mutations are applied at a step boundary, never against the running DAG. The
  composition is incremental in the changed region and its boundary: each region
  compiles once to a cached sub-graph whose interior (intra-region) edges are baked
  into the node objects and never re-touched while it is unchanged, and dependencies
  that cross a region boundary live in a separate layer on the node
  (`Node::region`, a `RegionLink`, walked by the scheduler's
  `trigger_successors`). When a region streams in or out only its own nodes are
  rebuilt and only the boundaries of the allocations it *shares* are re-stitched, so
  a commit costs O(changed region + affected boundary pins), independent of the size
  of the untouched world. Cross-region edges are derived per shared allocation in
  ascending region-key order (the lower-keyed region is the producer), honouring
  RAW/WAR/WAW while leaving read/read sharing parallel; the root set and the
  completion sentinel's leaf links are maintained incrementally. `compile_count()`
  holds at 1 across an unmutated multi-step run and advances by exactly one per
  mutation, not per region. New `TaskGraph::take_nodes()`, `Node::arm_in_degree` /
  `RegionLink::clear`, and `Region` / `DynamicGraph` API headers
  (`api/graph/region.hpp`, `api/graph/dynamic_graph.hpp`). The runtime owns the
  mechanism; *which* region streams when is a consumer (SushiEngine) policy.
- Cross-context co-location guard (§5.4): a single SYCL kernel cannot dereference USM
  from two non-peer device contexts. The scheduler now verifies, on a node's first
  dispatch, that all of its buffers share one device context, and fails the run with a
  precise `invalid_graph` error if they straddle a boundary — instead of letting the
  kernel hit an illegal cross-context access. The result is cached per node, so
  steady-state replay pays nothing. Full cross-device execution (splitting data with
  the work via region ownership and halo exchange) is deferred to WP-5; this enforces
  the co-location contract until then. New `RuntimeContext::context_index_for_ptr`.
- Game-engine worked example (`examples/game_engine_demo.cpp`): a 4096-entity
  particle world — gravity, integration, floor collision, and deferred culling — built
  entirely on the fluent API, where the four systems are ordered purely by the
  component buffers they read and write (the dependency tracker is the system
  scheduler), compiled once and replayed per frame, and checked against a scalar
  reference. A concrete demonstration that the task graph behaves like a game engine.
- Reorganised the `examples/` tree onto the fluent API under one naming rule —
  `quickstart.cpp` plus `<topic>_demo.cpp`: `quickstart` (1-D heat State),
  `vecadd_demo` (buffers and automatic dependency ordering), `stencil_demo` (2-D
  interior-only stencil, formerly `fluent_stencil_2d.cpp`), `coupled_fields_demo`
  (Jacobi coupled States), `game_engine_demo` (the ECS frame loop, moved out of the
  test suite), `streaming_world_demo` (a dynamic region graph that streams tiles in
  and out), and `distributed_demo` (unchanged). The old `fluent_quickstart.cpp` and
  `fluent_stencil_2d.cpp` and their `sr_fluent_*` targets are replaced; the
  `test_game_engine_example` integration test is removed in favour of the runnable
  example.
- RunReport observability (§5.5): opt-in profiling, chosen once at
  `Runtime::create({}, cfg)` with `RuntimeConfig::profiling = true`. When on, a run
  report carries a per-node breakdown (`node_timings`: device vs host time and
  invocation count per system, keyed by a stable plan index) and a per-worker stall
  split (`worker_timings`: busy / stealing / polling / idle), and fills the
  previously-reserved `scheduling_overhead_ms` from completion-polling time. Device
  timing comes from SYCL event profiling (queues are built with profiling enabled
  only in this mode); a backend without profiling support keeps the counts and skips
  device time. Off by default: queues and the dispatch hot path are as before,
  reading no timestamps, and the report's profiling vectors stay empty. Explicit
  host<->device transfers issued by `Buffer::read_range`/`write_range` are always
  accounted (`host_to_device_bytes` / `device_to_host_bytes`, attributed to the run
  window); implicit shared-USM driver migration is not observable through SYCL and is
  deliberately not reported.
- Fluent API reference site: a repo-root `Doxyfile` scoped to the public
  `include/SushiRuntime/api` surface, a hand-written manual landing
  (`docs/api/mainpage.md`), and the getting-started guide build together into a
  browsable scripting-style reference. Build it with `sr doxygen`
  (output in `docs/api-site/html`); a CI job builds it on every push/PR and
  surfaces any undocumented public symbol.
- Rebalancer opt-out: `Runtime::rebalancer(bool)` (and
  `ExecutionEngine::set_rebalancer_enabled` / `rebalancer_enabled`) turns the
  background thermal rebalancer on or off. Off by default (see Changed); turn it
  on for a throughput batch that can absorb the mid-run background thread and its
  run-to-run nondeterminism.
- Device-resident state: `Buffer<T>`/`State<T>` (and the `rt.buffer<T>()` /
  `rt.state<T>()` factories) take an optional `Residency` — `Shared` (the default,
  host-addressable) or `Device` (stays resident on its device, no host migration).
  Device-resident handles are not host-indexable (`operator[]` asserts in debug);
  the host seeds and reads them through `write_range(offset, src, count)` and
  `read_range(offset, count)`, the selective-readback seam that copies just a
  window through the owning queue. Backed by `RuntimeContext::malloc_device_on`.
  Shared residency and existing code are unchanged.
- Native CPU execution path: `Graph::add_host(...)` runs arbitrary host C++ that
  submits no SYCL work (gameplay logic, AI, navigation, CPU-side broadphase)
  directly on a worker thread, with no queue submission. The task is ordered into
  the graph by the `Reads(...)`/`Writes(...)` it names (an untracked overload runs
  every step), and completion flows through the same successor-release path as a
  kernel — one completion contract, not a second scheduler. Backed by a
  `Task::submits_device_work()` hint (safe default true) and `Task::execute_native()`;
  the scheduler skips queue acquisition only when a node opts in. Throwing native
  tasks fail the run through the existing error-isolation path and leave the pool
  re-armable.
- Late-bound graph tasks (persistent graph, no per-step recompile): a `Dynamic`
  tag — built with `API::when(predicate)` and/or `API::sized(count)` and passed
  as the leading argument to `Graph::add()` — lets a task's per-step **enablement**
  and **iteration size** change every step while the graph is compiled exactly
  once and replayed. A disabled node runs no kernel and instantly releases its
  successors (a sleeping island / LOD-off system); `sized()` iterates a live
  prefix within the buffers' baked capacity. Available on the per-element
  (`Reads`/`Writes` and raw-pointer) and 1-D `State` add overloads. The controls
  live in a shared `Graph::LateBindings { enabled, count }` block the scheduler
  and kernel read at dispatch. `Graph::compile_count()` exposes the replay
  guarantee (stays 1 across such a run). Bindings may vary size/enable but not
  which resources a node touches — that still requires a recompile.
- Region-based dependency identity: the `DependencyTracker` now orders accesses
  by **byte-interval overlap** (`Core::ResourceRegion { base, offset, length }`),
  not bare base-pointer equality. Disjoint sub-ranges of one allocation (an
  array's halves, a grid's interior versus its halo) run in parallel, while
  overlapping ranges are still ordered. `Buffer<T>::region(offset, count)` names
  a sub-range; pass it to `Reads(...)`/`Writes(...)`. A bare pointer remains the
  whole-buffer region, so existing graphs are unchanged.
- Fluent stepped API for the v1 surface: `Runtime::buffer<T>()` / `state<T>()`
  typed shared-USM handles; handle-based `Graph::add()` with automatic
  dependency tracking, including `add(state, fn)` stepped update rules and
  coupled multi-field rules `add(out, in0[, in1], fn)` (Jacobi semantics).
- `Graph::run(n)`, `run_for(duration)`, and `run_until(predicate)` plus the
  non-throwing `try_run(n)` — the graph is compiled once and replayed across
  steps (compile-once / replay-many), with per-step node-state reset and
  double-buffering of `State<T>` fields.
- `RuntimeContext::malloc_shared_on(bytes, device_index)` — shared USM pinned to
  a specific device queue, so buffers used together share one context.

### Changed
- **The scheduler no longer downcasts a `Task*` to a `Graph::Node*`, and its
  header no longer includes the graph layer's.** `Task` gained
  `on_complete(ReadySink&)` and `record_completion(device_ns)`; `Node` overrides
  both and walks its own inline, spillover and cross-region successors. The
  defect was a layering inversion — `scheduler/event_polling.hpp` included
  `graph/node.hpp`, so a lower layer pulled in a higher one — and it is gone;
  `EventPolling` now compiles without the graph layer entirely.

  `ReadySink` is a reused buffer rather than a callback interface, and that was
  measured, not assumed. A virtual call per freed successor looks free on paper
  next to the three atomics already on that path, and cost **4.6x** on
  `Performance_WideFanout` (286 → 1303 ns/task): what it removes is not a few
  cycles of dispatch but the inlining of a loop that previously ran end to end
  into the concrete queue. Collecting into a buffer leaves one virtual call per
  completion and keeps the push inlined — five matched samples per build put the
  result inside run-to-run noise (means 308.3 vs 314.1 ns/task, ranges
  overlapping). The five remaining downcasts in `scheduler.cpp` stay: each reaches
  for a different `Node` member, and virtualising all of them would make `Task` an
  interface encoding every graph concern.
- **An idle worker pool now actually idles.** The armed flag was set on the first
  run and never cleared until the context was destroyed, so workers never reached
  the dispatch loop's wait: between runs — and for the whole life of a runtime that
  had run once — every worker sat in the steal backoff, yielding and sleeping in
  100 µs increments and sweeping every peer deque thousands of times a second for
  work that was not coming. `TaskScheduler::disarm()` (called by
  `ExecutionEngine::complete()` once the run has quiesced, the only point where it
  is safe) returns the pool to a parked state. On a machine the runtime shares with
  a host's render, audio and job threads, that is the difference between idling and
  competing.
- The idle wait lost its 10 ms timeout, which was covering a lost wakeup rather than
  tuning anything: `arm_for_execution()` and `stop()` published their flags outside
  `pending_mutex` while the wait predicate read them under it, so a notify could land
  in the window between a worker evaluating the predicate and blocking. The timeout
  bounded the damage to 10 ms of late start per occurrence — on a 16 ms frame budget,
  a jitter source with no error message. Both now publish under the mutex, so the
  wait needs no fallback and a parked worker costs nothing.
- `SR_DISTRIBUTED` stays a PUBLIC define, and the comment claiming it was PUBLIC only
  because of `Graph::Node`'s layout has been replaced with the actual reasons. The
  layout coupling was indeed fixed by `NodeExtensions`, but two others were not: the
  library compiles none of `include/SushiRuntime/api/`, so every `#ifdef
  SR_DISTRIBUTED` there — `Buffer`/`State` registration, the whole cluster API,
  `DynamicGraph::rearm()`'s offload-attempt reset — is consumer-compiled surface that
  a private define would silently remove; and `NodeExtensions` still changes size with
  the flag while its deleter is instantiated wherever a `Node` is destroyed, which
  includes consumer code.
- `Core::Result<T, E>` is declared **permanent**, closing the open question of whether
  to wait for C++23 and swap to `std::expected`. It is in the public return type of
  every `try_*` entry point, so deferring the decision only moved a breaking change
  to the language bump; its shape already mirrors `std::expected`, so if the baseline
  ever moves the migration is an alias and consumers see nothing.
- The injector drain no longer allocates: the batch buffer moved from a function
  local to a per-worker member with retained capacity, removing one heap allocation
  per worker per drain from the per-tick path.
- **The background thermal rebalancer is off by default** (behaviour-breaking).
  It migrates tasks between workers on a 5 ms heartbeat, which changes which worker
  folds which tile and puts a periodic wakeup under every frame — a throughput trade
  a simulation frame is not buying anything with, and one every consumer was paying
  for while the primary one turned it off at construction. Set
  `RuntimeConfig::rebalancer` (or call `Runtime::rebalancer(true)`) to restore it.
- Build flags no longer leak to consumers. `-O3`, `-march=native` and `-ftls-model`
  were applied through a directory-scope `link_libraries()`, which lands in the
  library's link interface — an installed target would have exported `-Werror` and a
  host-specific `-march` to whoever linked it, and the export itself would not have
  configured. Policy is now applied per target and `PRIVATE` via
  `sushiruntime_apply_build_policy()`, split across `cmake/CompilerWarnings.cmake`
  (warnings), `cmake/Optimization.cmake` and `cmake/Determinism.cmake` behind one
  `cmake/BuildPolicy.cmake` entry point. Two consequences worth stating: `-march=native`
  is now opt-in (`SR_NATIVE_ARCH`, default OFF) because a natively-tuned binary is not
  a shippable one, and `-O3` is gone entirely — CMake's own per-configuration flags
  supply the optimisation level, so a Debug build is genuinely `-O0` again instead of
  being optimised behind the developer's back.
- `/fp:fast` is gone from the Windows build (superseded by `SR_DETERMINISTIC_FP`).
  It licensed reassociation and FMA contraction, so a host-side fold could produce
  different bits from build to build — directly against the replay and rollback
  guarantee the API documents.
- Build identifiers carry the full program name: `sushi_warnings` / `sushi_sanitizers`
  and the rest are now `sushiruntime_`-prefixed, a SushiStack-wide convention. The old
  names collided across sibling repositories built in one tree.
- GPU backend selection is now vendor-agnostic: a single `SR_GPU_BACKEND`
  (`auto`/`cuda`/`rocm`/`intel`/`cpu`/`none`) resolves in a modular `cmake/gpu/`
  layer to one vendor strategy, with auto-detect probing the host compiler
  toolchain (and off-`PATH` `nvcc`/`hipcc` install prefixes). Adds ROCm/AMD and
  Intel GPU backends alongside CUDA; `SR_CUDA_ARCH` is auto-detected from
  `nvidia-smi`. `-DSR_ENABLE_CUDA=ON` is kept as a legacy alias for
  `SR_GPU_BACKEND=cuda`.
- CLI presentation moved to the shared `sushicli` layer; the CLI is installed with
  `ss install-cli sushiruntime` (the old `cli/install.py` script was removed).
- API hardening pass (source-breaking, no API freeze). `Buffer`/`State`/`Runtime`
  `buffer()`/`state()` take a `DeviceIndex` instead of a bare `std::size_t` device
  argument; `read_range`/`write_range`/`Buffer::region` take an `ElementRange` instead
  of bare offset/count pairs (`buf.read_range({0, n})`, `buf.region({off, len})`).
  `Buffer::operator[]` and the new `host()` throw `invalid_access` on a device-resident
  handle in every build, where indexing was previously debug-asserted and undefined in
  release. `Dynamic` is now move-only (its providers are `Delegate`s): pass a
  `when()`/`sized()` temporary, or `std::move` a named `Dynamic`, into `add()`.
  `Runtime::context()` is renamed `Runtime::unsafe_context()` to flag the
  encapsulation escape hatch; routine reads move to `Runtime::advanced()`.
- Grouped `Graph::Node`'s non-core concerns into cohesive sub-structs so each
  subsystem owns its own slice instead of editing one flat node: the dynamic graph's
  cross-region boundary fields move into `RegionLink Node::region`, the profiling
  accumulators into `NodeProfiling Node::profiling`, and the distributed offload state
  (distributed builds only) into `OffloadState Node::offload`. Field accesses are now
  `node.region.boundary_successors`, `node.profiling.invocations`,
  `node.offload().offloadable`, and so on; `Node::clear_boundary()` became
  `RegionLink::clear()`. Internal-only — no public API, behaviour, or hot-path change;
  the scheduler still walks the same inline fields at the same offsets.
- Made `Graph::Node`'s layout independent of `SR_DISTRIBUTED`. The offload state now
  lives in a lazily-allocated `NodeExtensions` reached through one unconditional
  `Node::ext_` pointer (`OffloadState Node::offload()` / `offload_state()`), instead
  of an `#ifdef`'d inline member that changed `sizeof(Node)` between builds. A
  standalone build never allocates the extensions and pulls in no distributed symbol;
  a distributed build allocates them lazily when a node first records offload state.
  This removes the build-flag ABI coupling on `Node` — the library and its consumers
  now agree on `sizeof(Node)` whether or not the distributed layer is compiled in.
- Reusable completion latch (§5.1): `ExecutionEngine::execute` no longer allocates a
  `std::promise`/`std::future` pair (and a capturing sentinel closure) per step. One
  mutex+condition_variable latch lives on the engine and is reset, armed, and waited
  on each step with no heap allocation; the sentinel's `host_work` is wired once per
  compiled plan instead of every call. Over a long replayed run this removes millions
  of per-step allocations and the associated jitter — the real-time path (L1) — while
  keeping the existing completion contract: the sentinel still drains the DAG,
  re-arms the pool, and surfaces a task error to the caller (now rethrown from the
  latch), so error isolation and cancellation behave exactly as before. The
  always-empty `sycl::event` that `execute()` returned is unchanged (no caller used
  it); the per-step event-promise plumbing behind it was removed.
- The scheduler's per-task downcast no longer uses RTTI: `execute_task` resolves a
  graph node through the virtual `is_graph_node()` tag and a `static_cast` instead
  of `dynamic_cast`, removing the one hot-path RTTI query and matching the strategy
  `EventPolling` already used.
- `IExecutionEngine::execute` is now non-consuming: `execute(CompiledGraph&)`
  runs a graph in place (enabling replay), with an rvalue convenience overload
  for one-shot runs. Existing call sites that pass rvalues are unaffected.
- `TaskGraph::add_node(work, reads, writes)` now retains the access lists on the
  node, matching `add_task`, so the scheduler routes the kernel to the queue that
  owns its USM.
- `TaskGraph::add_node` / `add_task` read/write parameters and `Node::io_reads_` /
  `io_writes_` are now `std::vector<Core::ResourceRegion>` rather than
  `std::vector<void*>`. Base pointers convert implicitly to whole-buffer regions,
  so bare-pointer call sites (`{a, b}`, `{C}`) compile unchanged.
- README, INTRODUCTION, and ARCHITECTURE lead with the fluent API; the
  task-graph API is documented as the lower-level foundation.

### Fixed
- **The Doxygen site's manual half was silently not building.** `Doxyfile`'s
  `INPUT` named `docs/guides/INTRODUCTION.md`, and there is no `docs/guides/`
  directory — the reference built from the headers while the hand-written pages
  were skipped without an error. Same stale prefix in `docs/api/mainpage.md`'s
  cross-references. `docs/INTRODUCTION.md`'s three `../../README.md` links
  resolved two levels above the repository root. All corrected, and
  `INTEGRATION.md` / `CLI_GUIDE.md` joined the site so the manual's links resolve.
- Memory-safety hardening of the concurrency and distributed layers:
  - EBR `enter()` now issues a `seq_cst` fence after publishing its epoch slot, so
    a reader's slot is globally visible before it dereferences shared structure —
    closing a Dekker-style store/load race that could let a concurrent reclaim
    free a node still in use.
  - `EpochGuard` is re-entrant: a nested guard no longer clears the thread's epoch
    slot mid-critical-section (per-thread nesting counter).
  - Per-worker poll backoff/stall state moved off the shared `EventPolling`
    instance onto each worker's `ActiveEventList`, removing a data race that could
    wrap a counter and hang a run.
  - Documented the recorded-handle move hazard: a graph records a `Buffer`/`State`
    by object address, so moving one after `Graph::add()` (or reallocating a
    container that holds it) leaves the graph pointing at the old address. The move
    operations and `add()` now warn against it (the liveness token travels with the
    value by design, so this case cannot be auto-detected).
  - `WorkloadCoordinator::set_strategy()` is synchronised against the monitor
    thread, fixing a use-after-free when swapping strategies at runtime.
  - `ExecutionEngine::execute()` rejects a concurrent run instead of corrupting the
    shared completion latch.
  - `TaskGraph::compile()` drains the dependency tracker before throwing on a
    root-less graph (and `DependencyTracker` reclaims on destruction), preventing a
    use-after-free on a later `add_node()` after a failed compile.
- Distributed wire protocol robustness (unauthenticated trust boundary):
  - `ByteReader::get_blob` bounds-checks the length prefix without an overflowing
    addition, so a hostile 64-bit length can no longer yield an out-of-bounds span
    or a `~2^64`-byte allocation.
  - The worker's `BUFFER_PUSH` handler clamps its `memcpy` to the allocated size
    (matching the master), closing a heap/USM overflow from mismatched wire fields.
  - Cache-buffer allocations are validated against `MAX_FRAME_BYTES` and their
    null returns handled; the worker message pump contains any deserialize/alloc
    throw instead of terminating the process.
  - The scheduler routes a throw from `poll_events` through `record_run_error`
    rather than letting it escape the worker thread and terminate the process.
  - `TaskDispatchMsg` no longer serializes `TaskMetadata::name` (a raw `const char*`)
    onto the wire — a wild pointer on the worker and an ASLR leak; the POD fields
    are serialized field-by-field and the name restored to a default.
- Correctness:
  - `TaskGraph::compile()` now rejects dependency cycles. A non-empty root set is
    necessary but not sufficient; a disjoint cycle would leave nodes unschedulable
    and hang the run on completion. A Kahn's-algorithm reachability pass (paid once
    per compile, amortized over every replay) detects and fails it loudly.
  - Per-worker completed-task counters are keyed by the executing worker id, not the
    EBR slot id (which shifts when a non-worker thread touches the reclaimer first),
    so the thermal rebalancer reasons over the right worker's rate.
  - Pool pages are allocated with `aligned_alloc_shared(DEFAULT_ALIGNMENT, …)` so a
    pooled block honours an alignment request up to `DEFAULT_ALIGNMENT` (a plain
    `malloc_shared` only guaranteed the backend's natural alignment).
  - `Buffer` construction throws `allocation_failed` on OOM instead of silently
    holding a null pointer (which would drop the region from the tracker and then
    fault in-kernel).
  - `add_successor` publishes the successor slot before the count (release), closing
    a latent window where a concurrent `trigger_successors` could see an
    incremented count but a still-null slot and skip a decrement.
  - `Node::is_fused` (never read or written) and the "kernel fusion" claim in
    `TaskGraph`'s docs are removed — an advertised feature that did not exist.
  - `~Runtime` swallows a late async device error from `wait_and_throw()` instead of
    terminating the process from a noexcept destructor; `USMManagerImpl` stores its
    queue by value (was a dangling reference in teardown); the allocator registry
    slot is cleared with a compare-exchange so a slot-colliding live allocator is not
    unregistered; `TaskScheduler::start()` is guarded against double-spawn.
  - TCP master bounds accepted connections with `SO_RCVTIMEO`, so a peer that
    connects but never sends cannot hold a worker slot forever.
- Performance:
  - The global `pending_mutex` is no longer taken on every dispatch iteration: two
    relaxed shadow counters let a worker skip the lock when the injector queues are
    empty (the steady state), de-serializing submit throughput on many cores.
  - Per-step INFO logging in `execute()` dropped to DEBUG (compiled out in release),
    so a million-step `run(n)` no longer emits millions of log records.
  - `worker_completed_tasks` is one cache line per counter (was false-shared) and
    zero-initialized (was indeterminate in C++17).
  - The poll loop caches whether an event is real (`has_event`) instead of
    constructing a throwaway `sycl::event{}` per entry per poll.
- Build/CI:
  - Cleared the stale compile-feature requirement on the imported GTest targets so a
    bleeding-edge Clang (whose feature table CMake doesn't know) can configure the
    test suite instead of failing at generate.
  - The functional CI lane builds the tests (`SR_BUILD_TESTS=ON`) and the distributed
    layer (`SR_ENABLE_DISTRIBUTED=ON`) — previously it built and ran nothing; added a
    ThreadSanitizer lane and a Python CLI lint/test lane.
- Tests: added malformed-input/fuzz coverage for the wire protocol (overflowing and
  truncated blobs, garbage frames); the allocator stress test now asserts global
  pointer uniqueness (no double-hand-out); the distributed fan-out test asserts the
  offload total rather than a flaky exact per-worker split; added a Python CLI test
  suite (lazy console + project-root resolution).
- CLI: the console is initialised lazily, so `sr --help`, `sr toolchain`, and other
  commands no longer abort with "Not inside a SushiRuntime project" when run outside
  a checkout. The reconfigure stamp now keys on `(distributed, build_type, gpu_backend,
  tests)`, so `sr build -t release` then `-t debug` reconfigures instead of silently
  reusing the Release tree; `sr test` clears an inherited `CUDA_LAUNCH_BLOCKING` (as
  `sr run` already did) so it cannot serialize the async/overlap tests. The tracked
  `cli/sushiruntime_cli.egg-info/` build artifacts were untracked.
- Flaky `Integration_FluentAPI.RunForRunsMultipleStepsWithinBudget`: the test asserted
  that a trivial kernel completes more than one step within a 50ms `run_for` budget, but
  `run_for` fixes its deadline before the first step, so the one-off JIT compilation and
  queue/device setup on a cold dispatch alone overran the budget and only one step ran.
  It passed only when an earlier test in the same process had already warmed the device,
  so it failed deterministically when `sr test` ran it in its own ctest process.
  The test now warms the kernel before timing, measuring steady-state throughput as
  intended.
- Fluent handle allocation no longer round-robins across devices: pinning each
  graph's data to one device fixes a cross-context illegal access (observed as
  `CUDA_ERROR_ILLEGAL_ADDRESS`) when a kernel touched two co-allocated buffers
  on a heterogeneous (GPU + CPU) machine.

---

<!--
## [x.y.z] — YYYY-MM-DD

### Added
- New features

### Changed
- Changes to existing behavior

### Fixed
- Bug fixes

### Removed
- Removed features

[x.y.z]: https://github.com/sushisystems/sushiruntime/compare/vX.Y.Z-1...vX.Y.Z
-->
