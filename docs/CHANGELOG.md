# Changelog

All notable changes to SushiRuntime are documented here.

Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) — versions follow [Semantic Versioning](https://semver.org/).

---

## [Unreleased]

### Added
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
