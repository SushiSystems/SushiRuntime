# Changelog

All notable changes to SushiRuntime are documented here.

Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) — versions follow [Semantic Versioning](https://semver.org/).

---

## [Unreleased]

### Added
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
  browsable scripting-style reference. Build it with `sr project doxygen`
  (output in `docs/api-site/html`); a CI job builds it on every push/PR and
  surfaces any undocumented public symbol.
- Rebalancer opt-out: `Runtime::rebalancer(bool)` (and
  `ExecutionEngine::set_rebalancer_enabled` / `rebalancer_enabled`) turns the
  background thermal rebalancer on or off. It stays on by default for throughput;
  turn it off for a low-jitter real-time frame or a deterministic/replay run,
  removing the mid-run background thread and its run-to-run nondeterminism.
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
- Fluent handle allocation no longer round-robins across devices: pinning each
  graph's data to one device fixes a cross-context illegal access (observed as
  `CUDA_ERROR_ILLEGAL_ADDRESS`) when a kernel touched two co-allocated buffers
  on a heterogeneous (GPU + CPU) machine.

### Removed
-

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
