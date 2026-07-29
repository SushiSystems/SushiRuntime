# SushiRuntime — Open Work

---

## Correctness (blockers before v1.0.0)

**#29 — ThreadLocalMagazine teardown ordering — RESOLVED**

The liveness check this item asked for was already in place, and is now complete.
A flush takes `flush_mutex()` shared, reads the allocator out of the registry and
verifies `owns_id`, so it no-ops once that allocator is gone; an allocator's
destructor takes the same lock exclusively for its whole teardown and clears its
registry slot (by compare-exchange, so a slot a collided id has since claimed is
left alone) before freeing pages. A thread outliving its `RuntimeContext`
therefore flushes into a null slot rather than into freed memory.

What was missing was one level further out. The lock was a namespace-scope
`std::shared_timed_mutex`, and a `thread_local` magazine's destructor runs at
*thread* exit — which, for a thread outliving `main()`, is after static
destruction. Locking a destroyed mutex in the very path that exists to make
teardown safe is the same use-after-free one level up. `flush_mutex()` now
allocates it once and never frees it; everything else this file keeps at
namespace scope is trivially destructible and already immune. Covered by
`Regression_Sentinel.MagazineFlushSurvivesRuntimeTeardown`.

**#30 — EventPolling 256-event cap — RESOLVED**

`ActiveEventList` no longer drops events. It keeps 256 entries inline (no
allocation in the common case) and spills the overflow into a heap vector; its
`push()` never fails and never drops. The 256 is now an inline fast-path capacity,
not a hard cap, so the deadlock this item described cannot occur.

**#33 — EventPolling `Task*` → `Node*` downcast**

`EventPolling` uses `is_graph_node()` to guard a `static_cast<Node*>` from a
`Task*`. This couples the scheduling hot path to a derived type. Inspect
`src/scheduler/scheduler.cpp`: if the cast is still there, add a virtual
`on_complete()` to `Task` so `EventPolling` stays type-blind and `Node` handles its
own successor release. If the code was already changed differently, close this item.

---

## API surface (before the v1.0.0 freeze)

**Fluent API dependency tracking — RESOLVED (Option A)**

Every launch shape now has a tracked overload — `add(Reads, Writes, n, range_fn)`,
`add(Reads, Writes, raw_kernel)` and `add(Reads, Writes, NdRange<N>, fn)` were the
three that were missing — and the five dependency-blind overloads are spelled
`add_untracked(...)`. Inference was never possible (a per-element callable captures
its USM pointers by value, so its type says nothing about what it touches), so
Option A is delivered as "always offer a tracked form, and make the untracked one
name itself" rather than as automatic detection. Rationale and the sweep are in
[`PHYSICS_SUBSTRATE_IMPLEMENTATION.md`](PHYSICS_SUBSTRATE_IMPLEMENTATION.md) §1.

**`Core::Result<T,E>` as the permanent public type**

`Core::Result<T,E>` was introduced as a C++17 shim for `std::expected`. A decision
is still open: keep `Result` as the official forever type (so the return shape never
breaks when the C++ standard is raised), or plan a migration to `std::expected` at
C++20/23. The public shape (`operator bool`, `.value()`, `.error()`, `*`) was kept
identical to `std::expected` to make either direction cheap. Document the decision
before the freeze.

---

## Release hygiene (before the v1.0.0 tag)

- **CMake version.** `project(SushiRuntime VERSION 0.1.0)` → `VERSION 1.0.0`.
  Create a matching git tag.
- **Orphaned comment.** `include/SushiRuntime/graph/task_graph.hpp` line 161
  contains a commented-out field that is a refactoring artifact. Remove it.
- **Reserved `RunReport` fields.** `scheduling_overhead_ms`,
  `network_latency_wait_ms`, `host_to_device_bytes`, `device_to_host_bytes`,
  `tasks_received_from_network`, `network_bytes_tx`, `network_bytes_rx` are always
  zero. Each should carry `@note Reserved: always 0 in v1.` in its Doxygen so users
  reading the header directly understand why the measurement is absent.
- **`SR_BUILD_TESTS` default.** The CMake option defaults to `OFF`, so a direct
  `cmake` invocation (without the CLI) produces a library-only build with no test
  binary. Either flip the default to `ON` or add an explicit note in the
  "Building with CMake directly" section of the README.

**Doxygen conformance sweep.** Every public function in `include/SushiRuntime/`
must conform to the rule in `CONTRIBUTING.md §4`: state why and how in plain
English, document every `@param` and `@return`, no separator comments. Do this as a
single behavior-free pass (no logic changes) before the API surface is frozen.

---

## Distributed layer — production hardening

The distributed core is complete and all tests pass. The following gaps are
documented in `DISTRIBUTED_GUIDE.md §12` as v1 limitations; they do not block the
tag but should remain accurately documented.

1. **Reconnect.** A worker that drops mid-run cannot rejoin. The worker-death
   fallback covers the failure, but the TCP connection is not re-established.
2. **Shutdown race.** The master's `accept()` loop blocks indefinitely if fewer
   workers than expected connect before `stop()` is called (self-pipe or
   non-blocking accept would fix this).
3. **Pipelining.** Communication and compute do not overlap: task N+1 is not
   transmitted while the worker runs N.
4. **Worker scheduler bypass.** Offloaded tasks are dispatched directly to
   `get_queue(0)` on the worker, bypassing the worker's NUMA-aware scheduler.
5. **`TcpTransport` minor issues.** Native-endian length prefix (homogeneous-cluster
   assumption); light data race on the `peers` vector between accept and send
   (benign in practice).
6. **Endpoint configuration.** Cluster addresses are direct CLI flags
   (`--master`/`--port`/`--workers`); there is no `nodes.txt` file support.

---

## Lower-priority structural debt

**#34 — Sentinel injection in `compile()` instead of `ExecutionEngine`**

`TaskGraph::compile()` links leaf nodes to a sentinel and returns a
`CompiledGraph`. The cleaner split would move sentinel injection into
`ExecutionEngine::execute()` so `compile()` returns a pure DAG. The current design
is correct; this is structural debt only.

**#36 — Host tasks go through SYCL `host_task`**

Tasks with `ExecutionMode::HOST` are submitted via `q.submit()` +
`sycl::host_task` (or `AdaptiveCpp_enqueue_custom_operation` on AdaptiveCpp).
This adds SYCL submission overhead to plain CPU work. Whether bypassing SYCL for
host work matters at v1 workload sizes is a judgment call. Document the current
behavior and the trade-off.

**#37 — No DAG cancellation — RESOLVED**

`Graph::cancel()` / `DynamicGraph::cancel()` exist (`api/graph/graph.hpp`,
`api/graph/dynamic_graph.hpp`), routing to `ExecutionEngine::cancel()`: the active
step drains without launching further work and the pool stays re-armable. Covered
by `Integration_Cancellation`.

---

## Future work (out of v1 scope)

**Dynamic-graph boundary refinements.** The dynamic region graph composes in
O(changed region + affected boundary pins). Sub-range boundary ordering is now
done — the entry/exit pins carry their byte intervals and every hazard test is an
interval overlap, so two regions writing disjoint element ranges of one buffer are
no longer serialized. One refinement remains: distributed offload is not stamped
for dynamic-graph nodes (the dynamic composer bypasses `TaskGraph::compile`, where
`stamp_offloadable` runs), so an offloadable op recorded on a region runs its local
fallback under `SR_ENABLE_DISTRIBUTED` instead of shipping to a worker. It does not
block the single-node real-time target.

**Python bindings.** A pybind11 (or nanobind) wrapper around the C++ fluent API
would allow direct interop with NumPy, PyTorch, and Jupyter. The binding entry
point would be `python/src/module.cpp` + `bind_runtime.cpp` + `bind_graph.cpp`,
gated by a `SR_BUILD_PYTHON_BINDINGS` CMake option. Zero-copy data transfer via
the Python Buffer Protocol or DLPack is the performance-critical piece. Not started.

**C++ standard migration.** The project targets C++17 for toolchain portability.
When the SYCL toolchain baseline supports it, features can be restored in two
stages. See `docs/guides/CPP_STANDARD_MIGRATION.md` for the per-feature checklist
(concepts, `std::span`, `std::barrier`, `std::format`, `std::source_location` at
C++20; `std::expected`, `std::println` at C++23).

